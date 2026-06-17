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
    
    // Hard block: ONLY the master thread can initialize the data writer!
    if (mWriter) {
      mWriter->RunStart(run, isMaster);
    }
  }
  else 
  {
    // Worker threads completely skip the custom DataWriter file setup.
    // Geant4's internal G4AnalysisManager handles worker data splitting automatically.
    G4cout << "G4WT" << G4Threading::G4GetThreadId() << " > Skipping custom DataWriter setup." << G4endl;
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
  if (isMaster)
  {
    if (mWriter) {
      mWriter->RunEnd(run, isMaster);
    }
  }
  // Workers safely exit without touching mWriter
}
}
