#if defined(QARRAY_DETECTOR_GEOMETRY_LEIDEN_II)
#include "DetectorConstruction_LEIDEN_II.hh"
#elif defined(QARRAY_DETECTOR_GEOMETRY_DSPX)
#include "DetectorConstruction_DSPX.hh"
#else
#include "DetectorConstruction_LEIDEN_II.hh"
#endif
#include "ActionInitialization.hh"

#include "G4RunManagerFactory.hh"
#include "G4MTRunManager.hh" // Ensure MT manager header is available
#include "G4RunManager.hh"
#include "G4SteppingVerbose.hh"
#include "G4Version.hh"
#include "G4UImanager.hh"
#include "G4PhysListFactory.hh"
#include "G4VisExecutive.hh"
#include "G4UIExecutive.hh"
#include "Randomize.hh"

#include "MCSampler.hh"

using namespace QArray;

int main(int argc, char **argv)
{
  // 1. Instantiate the CORRECT Multi-Threaded Run Manager
#ifdef G4MULTITHREADED
  auto* runManager = new G4MTRunManager; 
  // Set the default number of threads (e.g., 4 or your Mac's core count)
  runManager->SetNumberOfThreads(4);
#else
  auto* runManager = new G4RunManager;
#endif

  // 2. Detect interactive mode (if no arguments) and define UI session
  G4UIExecutive *ui = nullptr;
  if (argc == 1)
  {
    ui = new G4UIExecutive(argc, argv, "Qt"); // Explicitly pass "Qt" for macOS stability
  }

  // use G4SteppingVerboseWithUnits
#if G4VERSION_NUMBER >= 1100
  G4int precision = 4;
  G4SteppingVerbose::UseBestUnit(precision);
#endif

  // Set mandatory initialization classes
  runManager->SetUserInitialization(new DetectorConstruction());

  // Physics list
  G4PhysListFactory plFactory;
  G4VModularPhysicsList *physicsList = plFactory.GetReferencePhysList("QBBC_EMV");
  physicsList->SetVerboseLevel(0);
  runManager->SetUserInitialization(physicsList);

  // User action initialization (Spawns worker local setups in MT mode!)
  runManager->SetUserInitialization(new ActionInitialization());

  // Initialize visualization
  G4VisManager *visManager = new G4VisExecutive;
  visManager->Initialize();

  // Get the pointer to the User Interface manager
  G4UImanager *UImanager = G4UImanager::GetUIpointer();

  // Process macro or start UI session
  if (!ui)
  {
    // batch mode
    G4String command = "/control/execute ";
    G4String fileName = argv[1];
    UImanager->ApplyCommand(command + fileName);
  }
  else
    {
      // interactive mode
      G4String macroPath = "init_vis.mac";
      
      // Check if the file exists in the current directory
      std::ifstream infile(macroPath);
      if (!infile.good())
      {
        G4cout << "\n[INFO] init_vis.mac not found in current directory. Checking parent directory..." << G4endl;
        macroPath = "../init_vis.mac";
      }
      infile.close();

      // Now execute the guaranteed correct path BEFORE the thread states lock
      UImanager->ApplyCommand("/control/execute " + macroPath);
      
      ui->SessionStart();
      delete ui;
    }ui;
    }

  // Job termination
  delete visManager;
  delete runManager;
}