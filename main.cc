#if defined(QARRAY_DETECTOR_GEOMETRY_LEIDEN_II)
#include "DetectorConstruction_LEIDEN_II.hh"
#elif defined(QARRAY_DETECTOR_GEOMETRY_DSPX)
#include "DetectorConstruction_DSPX.hh"
#else
#include "DetectorConstruction_LEIDEN_II.hh"
#endif
#include "ActionInitialization.hh"

#include "G4RunManagerFactory.hh"
#include "G4MTRunManager.hh" 
#include "G4RunManager.hh"
#include "G4SteppingVerbose.hh"
#include "G4Version.hh"
#include "G4UImanager.hh"
#include "G4PhysListFactory.hh"
#include "G4VisExecutive.hh"
#include "G4UIExecutive.hh"
#include "Randomize.hh"

#include "MCSampler.hh"

// C++17 Standard Libraries for Robust Path Resolution
#include <filesystem>
#include <iostream>

using namespace QArray;
namespace fs = std::filesystem;

int main(int argc, char **argv)
{
  // 1. Instantiate the CORRECT Multi-Threaded Run Manager
#ifdef G4MULTITHREADED
  auto* runManager = new G4MTRunManager; 
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

  // Use G4SteppingVerboseWithUnits for newer versions
#if G4VERSION_NUMBER >= 1100
  G4int precision = 4;
  G4SteppingVerbose::UseBestUnit(precision);
#endif

  // Set mandatory initialization classes
  runManager->SetUserInitialization(new DetectorConstruction());

  // Physics list setup
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
    // Batch mode execution
    G4String command = "/control/execute ";
    G4String fileName = argv[1];
    UImanager->ApplyCommand(command + fileName);
  }
  else
    {
      // Interactive mode execution with hyper-robust path validation
      fs::path macroPath = "init_vis.mac";
      
      // Determine where the actual compiled binary lives
      fs::path exeDir = fs::absolute(argv[0]).parent_path();

      // Strategy 1: Check Current Working Directory (where your terminal is sitting)
      if (!fs::exists(macroPath))
      {
        // Strategy 2: Check the directory where the binary lives (e.g., build/)
        if (fs::exists(exeDir / "init_vis.mac"))
        {
          macroPath = exeDir / "init_vis.mac";
        }
        // Strategy 3: Check the parent of the binary directory (the repo root)
        else if (fs::exists(exeDir.parent_path() / "init_vis.mac"))
        {
          macroPath = exeDir.parent_path() / "init_vis.mac";
        }
        // Strategy 4: Classic relative fallback
        else if (fs::exists("../init_vis.mac"))
        {
          macroPath = "../init_vis.mac";
        }
        else
        {
          // Absolute Guardrail: Kill the run before workers spawn with 0 ntuples
          G4cerr << "\n========================================================="
                << "\n [CRITICAL ERROR] Cannot locate 'init_vis.mac'!"
                << "\n Checked standard paths relative to execution location:"
                << "\n 1. " << fs::current_path() / "init_vis.mac"
                << "\n 2. " << exeDir / "init_vis.mac"
                << "\n 3. " << exeDir.parent_path() / "init_vis.mac"
                << "\n\n Please ensure init_vis.mac is in your repository root."
                << "\n========================================================= \n" << G4endl;
          
          delete ui;
          delete visManager;
          delete runManager;
          return 1; // Safe exit code
        }
      }

      G4cout << "\n[INFO] Successfully located macro at: " << fs::absolute(macroPath) << G4endl;

      // Execute the valid path BEFORE the thread states lock
      UImanager->ApplyCommand("/control/execute " + macroPath.string());
      
      ui->SessionStart();
      delete ui;
    }

  // Job termination
  delete visManager;
  delete runManager;
  return 0;
}