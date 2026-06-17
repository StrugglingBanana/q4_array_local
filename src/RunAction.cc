#include "RunAction.hh"
#include "Metadata.hh"
#include "SensitiveDetector.hh"

#include "G4RunManager.hh"
#include "G4Run.hh"
#include "G4CsvAnalysisManager.hh"
#include "G4LogicalVolumeStore.hh"
#include "G4LogicalVolume.hh"
#include "G4UnitsTable.hh"
#include "G4SystemOfUnits.hh"

namespace QArray
{
  namespace
  {
#ifdef QARRAY_DETECTOR_GEOMETRY_DSPX
    constexpr const char* kSnScoringLogicalName = "DetectorSnCubeLogical";

    void ConfigureDSPXScoring()
    {
      auto meta = Metadata::GetInstance();
      const auto mode = meta->GetString("/QR/generator/mode");
      const G4bool scoreAllDSPXVolumes = (mode == "volumeScan");

      for (auto* logical : *G4LogicalVolumeStore::GetInstance())
      {
        auto* detector =
            dynamic_cast<SensitiveDetector*>(logical->GetSensitiveDetector());
        if (!detector || detector->GetName().find("dspx_") != 0)
          continue;

        const auto& logicalName = logical->GetName();
        const G4bool active =
            scoreAllDSPXVolumes || logicalName == kSnScoringLogicalName;
        detector->Activate(active);

        G4cout << "DSPX_SCORING_ACTIVE logical=" << logicalName
               << " detector=" << detector->GetName()
               << " active=" << (active ? "true" : "false")
               << " mode=" << mode << G4endl;
      }
    }
#endif
  }

  RunAction::RunAction() : G4UserRunAction(),
                           mPGen(nullptr), mWriter(new DataWriter())
  {
  }

  RunAction::RunAction(PrimaryGeneratorAction *pgen) : RunAction()
  {
    mPGen = pgen;
  }

  RunAction::~RunAction()
  {
    delete mWriter;
  }

  void RunAction::BeginOfRunAction(const G4Run *run)
{
    if (isMaster)
    {
        G4Random::showEngineStatus();
        
        // 1. Only the master thread should open the global file/stream!
        if (mWriter) {
            mWriter->RunStart(run, isMaster); 
        }
    }
    else 
    {
        // 2. Worker threads must NOT call standard file-opening routines 
        // unless your custom writer is specifically designed to create 
        // thread-safe independent files (e.g., test_t0.csv).
        
        // If your mWriter uses G4AnalysisManager internally, it handles this.
        // If it is entirely custom C++ (std::ofstream), you MUST handle thread isolation:
        if (mWriter) {
            // Option A: If your writer is built for MT, pass a thread ID
            // Option B: Skip completely if the Master handles all writing globally via thread-safe buffers
            mWriter->RunStartWorker(run); // Custom isolated worker setup
        }
    }

    // Initialize primary generator (safe for both master/workers)
    if (mPGen)
        mPGen->BeginOfRunAction();

#ifdef QARRAY_DETECTOR_GEOMETRY_DSPX
    ConfigureDSPXScoring();
#endif
}

  void RunAction::EndOfRunAction(const G4Run *run)
  {
    // end primary generator
    if (mPGen)
      mPGen->EndOfRunAction();

    // save output data
    mWriter->RunEnd(run, isMaster);

    // show Rndm status
    if (isMaster)
      G4Random::showEngineStatus();
  }

}
