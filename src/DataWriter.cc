#include "DataWriter.hh"
#include "Metadata.hh"

#include "G4Run.hh"
#include "G4Event.hh"
#include "G4Track.hh"
#include "G4Threading.hh" // Add this at the top of DataWriter.cpp if not present
#include "G4UIcmdWithAString.hh"
#include "G4UIcmdWithABool.hh"
#include "G4UIcmdWithADoubleAndUnit.hh"
#include "G4SystemOfUnits.hh"
#include "G4SDManager.hh"
#include "G4HCtable.hh"
#include "G4Version.hh"
#include "G4LogicalVolumeStore.hh"
#include "G4LogicalVolume.hh"

#include "G4Version.hh"
#if G4VERSION_NUMBER >= 1100
#include "G4AnalysisManager.hh"
#else
#include "G4GenericAnalysisManager.hh"
using G4AnalysisManager = G4GenericAnalysisManager;
#endif
#include <functional> // include this header for std::hash
#include <algorithm>
#include <cctype>
#include <time.h>

namespace QArray
{

  // some predefined reduce types
  inline double _reducebycopyno(HitData *hit) { return hit->copyno; }

  inline double _reducebyvolname(HitData *hit) { return std::hash<std::string>{}(hit->volname); }

  inline double _fullreduce(HitData *) { return 0; }

  inline double _reducebyancestor(HitData *hit)
  {
    // return hit->decayAncestor ? hit->decayAncestor->trackID : 0;
    G4int ancestorID = hit->decayAncestorHashID;
    if (ancestorID)
    {
      HitData *ancestor = hit;
      while (ancestor->decayAncestorHashID)
      {
        ancestor = ancestor->decayAncestor;
      }
      return ancestor->hashID;
    }
    return hit->hashID;
  }

  inline double _reducebydaughter(HitData *hit)
  {
    return hit->decayDaughter ? hit->decayDaughter->trackID : 0;
  }

  struct _reducebytime
  {
    static double twindow;
    double operator()(HitData *hit) { return std::floor(hit->t0 / twindow); }
  };
  double _reducebytime::twindow = 1 * ms;

  // function to list existing reducer options
  json ListReducers(DataWriter *writer, G4UIcommand *, const G4String &)
  {
    writer->PrintHitMappings();
    return json();
  }

  DataWriter::DataWriter() : bEnabled(true), bWriteSteps(false),
                             bWriteAllEvents(false), bSort(false)
  {
    // register options with the metadata instance
    auto meta = Metadata::GetInstance();
    meta->AddParamCommand<G4UIcmdWithAString>("/QR/output/filename",
                                              "Set base name for output files",
                                              "filename");
    meta->Set("/QR/output/filename", "test");

    meta->AddParamCommand<G4UIcmdWithAString>("/QR/output/filetype",
                                              "Set output file type",
                                              "filetype");
    meta->Set("/QR/output/filetype", "csv");

    // meta->AddParamCommand<G4UIcmdWithABool>("/QR/output/adduuid",
    //                                         "Append instance uuid to filename?",
    //                                         "append");
    // meta->Set("/QR/output/adduuid", true);

    meta->AddParamCommand<G4UIcmdWithABool>("/QR/output/mergeNtuples",
                                            "merge nutples in multithread mode",
                                            "merge");
    meta->Set("/QR/output/mergeNtuples", false);

    meta->AddParamCommand<G4UIcmdWithABool>("/QR/output/writeDecayAncestors",
                                            "Write decay ancestor for each hit?",
                                            "write");
    meta->Set("/QR/output/writeDecayAncestors", true);

    meta->AddParamCommand<G4UIcmdWithABool>("/QR/output/includeNonIonizingEdep",
                                            "add nonIonizingEdep to total edep?",
                                            "include");
    meta->Set("/QR/output/includeNonIonizingEdep", true);

    meta->AddParamCommand<G4UIcmdWithABool>("/QR/output/writeSteps",
                                            "Write info for each step",
                                            "write");
    meta->Set("/QR/output/writeSteps", bWriteSteps);

    meta->AddParamCommand<G4UIcmdWithABool>("/QR/output/writeAllEvents",
                                            "Write aux. hits for empty events",
                                            "write");
    meta->Set("/QR/output/writeAllEvents", bWriteAllEvents);

    meta->AddParamCommand<G4UIcmdWithABool>("/QR/output/sort",
                                            "Sort hits?",
                                            "sort");
    meta->Set("/QR/output/sort", bSort);

    auto *twincmd = meta->AddParamCommand<G4UIcmdWithADoubleAndUnit>("/QR/output/timeWindow", "Set time where hits are summed over", "window");
    twincmd->SetUnitCategory("Time");
    meta->Set("/QR/output/timeWindow", _reducebytime::twindow);

    auto *enablecmd =
        meta->AddParamCommand<G4UIcmdWithABool>("/QR/output/enable",
                                                "Enable/disable output altogether");
    enablecmd->SetParameterName("enabled", true);
    enablecmd->SetDefaultValue(true);
    meta->Set("/QR/output/enable", bEnabled);

    using namespace std::placeholders;
    meta->AddParamCommand<G4UIcmdWithAString>("/QR/output/addNtuple",
                                              "Add a new reduced output ntuple",
                                              "<name> <mapping> [<mapping2>...]",
                                              std::bind(&DataWriter::ParseNewOutputCmd, this, _1, _2));

    meta->AddParamCommand<G4UIcommand>("/QR/output/listMappings",
                                       "List available mappings for data reducer",
                                       std::bind(&QArray::ListReducers, this, _1, _2),
                                       meta->NullKey());

    meta->AddParamCommand<G4UIcmdWithAString>("/QR/output/setReduceLvl",
                                              "Sets the reduce level for the given reducer",
                                              "<name> <lvl>",
                                              std::bind(&DataWriter::ParseReduceLvlCmd, this, _1, _2));

    // meta->AddParamCommand<G4UIcmdWithAString>("/QR/tracking/addFateDetector",
    //                                           "Add a new FateDetector",
    //                                           "<name> [<particle> [<particle>...]]",
    //                                           std::bind(&DataWriter::ParseNewFateDetectorCmd, this, _1, _2));

    // meta->AddParamCommand("/QR/tracking/addFluxCounter",
    //                       "Add a flux counter to logical volume",
    //                       "volname",
    //                       std::bind(&DataWriter::AddFluxCounter, this, _1));

    // register default mapping functions
    RegisterHitMapping("copyno", DataMapper(_reducebycopyno));
    RegisterHitMapping("volname", DataMapper(_reducebyvolname));
    RegisterHitMapping("full", DataMapper(_fullreduce));
    RegisterHitMapping("time", DataMapper(_reducebytime()));
    RegisterHitMapping("ancestor", DataMapper(_reducebyancestor));
    RegisterHitMapping("daughter", DataMapper(_reducebydaughter));
  }

  DataWriter::~DataWriter()
  {
  }

  std::string timetostr(time_t t = 0)
  {
    if (!t)
      t = time(0);
    static char buf[200];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %Z", localtime(&t));
    return buf;
  }
void DataWriter::RunStart(const G4Run *run, bool isMaster)
{
    auto meta = Metadata::GetInstance();
    bEnabled = meta->Get<bool>("/QR/output/enable");
    if (!bEnabled)
      return;

    auto AMan = G4AnalysisManager::Instance();

    // =================================================================
    // PHASE 1: GLOBAL VS. THREAD-LOCAL CONFIGURATION
    // =================================================================
    
    // Purely Global Actions (STAYS MASTER ONLY)
    if (G4Threading::IsMasterThread())
    {
        auto filetype = meta->GetString("/QR/output/filetype");
        G4cout << "Output file type: " << filetype << G4endl;

        time_t starttime = time(0);
        meta->Set("start_time", starttime);
        meta->Set("start_time_string", timetostr(starttime));
        meta->Set("runid", run->GetRunID());

        if (filetype == "root") {
            AMan->SetNtupleMerging(meta->Get<bool>("/QR/output/mergeNtuples"));
        }
    }

    // Shared Configurations (ALL THREADS MUST EXECUTE THIS)
    // Every thread needs to compute its local fileName and query its own control flags
    fileName = meta->GetString("/QR/output/filename");
    if (run->GetRunID() > 0)
    {
      fileName += "_run";
      fileName += std::to_string(run->GetRunID());
    }

    {
      auto pos = fileName.find_last_of("/\\");
      auto start = (pos != G4String::npos) ? pos + 1 : 0;
      std::replace(fileName.begin() + start, fileName.end(), '.', '-');
    }

    HitData::bsWriteDecayAncestors = meta->Get<bool>("/QR/output/writeDecayAncestors");
    HitData::bsIncludeNonIonizingEdep = meta->Get<bool>("/QR/output/includeNonIonizingEdep");

    // CRITICAL: Moving these flags outside allows worker threads to evaluate the loops below correctly!
    bWriteSteps = meta->Get<bool>("/QR/output/writeSteps");
    bWriteAllEvents = meta->Get<bool>("/QR/output/writeAllEvents");
    bSort = meta->Get<bool>("/QR/output/sort");
    _reducebytime::twindow = meta->Get<double>("/QR/output/timeWindow");

    // =================================================================
    // PHASE 2: NTUPLE BOOKING (ALL threads must define identical structures)
    // =================================================================
    vecDetectors.clear();
    G4SDManager *sdm = G4SDManager::GetSDMpointer();
    G4HCtable *hctable = sdm->GetHCtable();
    int ntupleid;
    
    for (int i = 0; i < hctable->entries(); ++i)
    {
      SensitiveDetector *det = (SensitiveDetector *)(sdm->FindSensitiveDetector(hctable->GetSDname(i)));
      if (!det || !det->isActive())
        continue;
      
      vecDetectors.push_back(det);
      G4String basename = det->GetName();
      
      // Removed the duplicated master metadata block that was here
      
      if (!det->IsReducible())
        continue;

      for (auto &name_reducer : mReducers)
      {
        const G4String &name = name_reducer.first;
        const DataReducer &reducer = name_reducer.second;
        
        // This execution now successfully fires on worker threads because bWriteSteps/bSort are visible!
        ntupleid = AMan->CreateNtuple(basename + "_" + name, name);
        HitData::DefineNtuple(AMan, ntupleid, reducer.reducelvl);
        AMan->FinishNtuple(ntupleid);
      }
    }

    // VERIFICATION PRINT: Removed 'if (IsMasterThread)' so you can watch
    // every active thread report its local ntuple allocation status.
    G4cout << "Thread [" << G4Threading::G4GetThreadId() << "] DataWriter::RunStart: " 
           << AMan->GetNofNtuples() << " ntuples are defined" << G4endl;

    // =================================================================
    // PHASE 3: FILE OPENING (EVERY thread calls this independently)
    // =================================================================
    auto filetype = meta->GetString("/QR/output/filetype");
    G4String fullFileName = fileName + "." + filetype;
    
    if (!AMan->OpenFile(fullFileName))
    {
      G4ExceptionDescription msg;
      msg << "Thread " << G4Threading::G4GetThreadId() << " failed to open output file " << fullFileName;
      G4Exception("DataWriter::RunStart", "QRCode002", FatalException, msg);
      return;
    }
}

void DataWriter::RunEnd(const G4Run *run, bool isMaster)
{
    if (!bEnabled) 
      return;

    auto *AMan = G4AnalysisManager::Instance();
    
    // EVERY thread must write and close its own local buffers
    AMan->Write();
    AMan->CloseFile();
    
#if G4VERSION_NUMBER >= 1100
    AMan->Clear(); 
#endif

    // Only the master thread compiles the global runtime performance metadata
    if (G4Threading::IsMasterThread())
    {
        vecDetectors.clear();
        auto meta = Metadata::GetInstance();
        time_t starttime = meta->Get<long>("start_time");
        time_t endtime = time(0);
        time_t duration = endtime - starttime;
        G4cout << "Run completed in " << duration << " seconds." << G4endl;
        meta->Set("end_time", endtime);
        meta->Set("end_time_string", timetostr(endtime));
        meta->Set("runtime_wall", duration);
        meta->Set("nevents", run->GetNumberOfEvent()); 
        meta->SaveCommandHistory();
        
        G4String metafile = fileName + ".json";
        if (Metadata::GetInstance()->Write(metafile))
          G4cout << "Metadata saved to " << metafile << G4endl;
        else
          G4cerr << "ERROR trying to save metadata to file " << metafile << G4endl;
    }
}

  void DataWriter::EventStart(const G4Event *)
  {
    // anything to do here?
  }

  void DataWriter::EventEnd(const G4Event *)
  {
    if (!bEnabled) // no-op
      return;

    auto *AMan = G4AnalysisManager::Instance();
    if (!bWriteAllEvents)
    {
      // first make sure the event is not empty
      bool empty = true;
      for (auto *det : vecDetectors)
      {
        QRHitsCollection *hc = det->GetHitCollection();
        if (hc && hc->GetSize())
        {
          empty = false;
          break;
        }
      }
      if (empty) // nothing to do
        return;
    }

    // record everything
    int ntupleid = AMan->GetFirstNtupleId();
    for (auto *det : vecDetectors)
    {
      QRHitsCollection *hc = det->GetHitCollection();

      if (bWriteSteps || !det->IsReducible())
      {
        if (hc)
          mAllStepsWriter.Write(AMan, ntupleid, hc, bSort);
        ntupleid++;
      }
      if (!det->IsReducible())
        continue;
      for (auto &name_reducer : mReducers)
      {
        if (hc)
        {
          DataReducer &reducer = name_reducer.second;
          reducer.Write(AMan, ntupleid, hc, bSort);
        }
        ntupleid++;
      }
    }
  }

  bool DataWriter::RegisterHitMapping(const G4String &name, const DataMapper &mapping)
  {
    return mMappers.insert(std::make_pair(name, mapping)).second;
  }

  void DataWriter::PrintHitMappings()
  {
    G4cout << "Available mappings: \n";
    for (auto mapper : mMappers)
    {
      G4cout << '\t' << mapper.first << '\n';
    }
    G4cout << G4endl;
  }

  bool DataWriter::RegisterReducedOutput(const G4String &name,
                                         const DataReducer &reducer)
  {
    return mReducers.insert(std::make_pair(name, reducer)).second;
  }

  json DataWriter::ParseNewOutputCmd(G4UIcommand *, const G4String &newval)
  {
    std::stringstream ss(newval);
    G4String name;
    if (!(ss >> name))
    {
      G4cerr << "ERROR: DataWriter::ParseNewOutputCmd: expected format <name> <reducer> [<reducer2>, ...]"
             << G4endl;
    }
    DataReducer reducer;
    G4String mapname;
    while (ss >> mapname)
    {
      auto mapitr = mMappers.find(mapname);
      if (mapitr == mMappers.end())
      {
        G4cerr << "ERROR: DataWriter::ParseNewOutputCmd: unknown mapping '" << mapname << "'"
               << G4endl;
      }
      reducer.AddMapping(mapitr->second);
    }
    if (reducer.vecMappers.empty())
    {
      G4cerr << "No mappings found: Format must be <name> <reducer> [<reducer2>, ...]"
             << G4endl;
    }

    if (!RegisterReducedOutput(name, reducer))
    {
      G4cerr << "ERROR: DataWriter::ParseNewOutputCmd: Reducer with name '" << name
             << "' already registered." << G4endl;
    }

    // this is just a dummy that we'll update later
    return newval;
  }

  json DataWriter::ParseReduceLvlCmd(G4UIcommand *, const G4String &newval)
  {
    std::stringstream ss(newval);
    G4String name;
    if (!(ss >> name))
    {
      G4cerr << "ERROR: DataWriter::ParseReduceLvlCmd: expected format <name> <lvl>"
             << G4endl;
    }
    // Find the reducer with name
    auto reduceritr = mReducers.find(name);
    if (reduceritr == mReducers.end())
    {
      G4cerr << "ERROR: DataWriter::ParseReduceLvlCmd: Reducer with name '" << name
             << "' not found." << G4endl;
    }
    DataReducer reducer = reduceritr->second;
    ss >> name;
    // Sets the reduce level

    // Convert the string to all uppper case
    // Finds the string; if not found, output an error
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    auto lvlitr = HitData::csRLVLTABLE.find(name);
    if (lvlitr == HitData::csRLVLTABLE.end())
    {
      G4cerr << "ERROR: DataWriter::ParseReduceLvlCmd: Reduce level '" << name
             << "' not found."
             << " Available reduce levels are: " << G4endl;
      for (const auto &lvl : HitData::csRLVLTABLE)
      {
        G4cerr << lvl.first << G4endl;
      }
    }
    else
    {
      reducer.reducelvl = lvlitr->second;
    }

    // this is just a dummy that we'll update later
    return newval;
  }

  // bool DataWriter::AddFateDetector(const G4String &name,
  //                                     const std::vector<G4String> &particles)
  // {
  //   auto *det = new FateDetector(name);
  //   for (const G4String &pname : particles)
  //   {
  //     det->AddParticle(pname);
  //   }
  //   fFateDetectors.push_back(det);
  //   return true;
  // }

  // bool DataWriter::AddFluxCounter(const G4String &volname)
  // {
  //   G4LogicalVolumeStore *lstore = G4LogicalVolumeStore::GetInstance();
  //   G4LogicalVolume *lvol = lstore->GetVolume(volname, false);
  //   if (!lvol)
  //   {
  //     // allow shorthand without l_ prefix
  //     lvol = lstore->GetVolume(G4String("l_") + volname, false);
  //   }
  //   if (!lvol)
  //   {
  //     G4ExceptionDescription msg;
  //     msg << "No volume with name " << volname;
  //     G4Exception("DataWriter::AddFluxCounter", "1", FatalException, msg);
  //   }
  //   lvol->SetSensitiveDetector(new FluxCounter(volname + "_flux"));
  //   return true;
  // }

  // json DataWriter::ParseNewFateDetectorCmd(G4UIcommand *, const G4String &newval)
  // {
  //   std::stringstream ss(newval);
  //   G4String name;
  //   if (!(ss >> name))
  //   {
  //     G4ExceptionDescription msg;
  //     msg << "Format must be <name> [<particle> [<particle> ...]]";
  //     G4Exception("DataWriter::ParseNewOutputCmd", "1", FatalException, msg);
  //   }
  //   std::vector<G4String> particles;
  //   G4String pname;
  //   while (ss >> pname && !pname.empty())
  //     particles.push_back(pname);
  //   AddFateDetector(name, particles);
  //   return newval;
  // }
}
