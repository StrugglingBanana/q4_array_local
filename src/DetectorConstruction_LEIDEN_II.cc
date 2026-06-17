#include "DetectorConstruction_LEIDEN_II.hh"
#include "SensitiveDetector.hh"
#include "Metadata.hh"

#include "G4RunManager.hh"
#include "G4NistManager.hh"
#include "G4SDManager.hh"
#include "G4Box.hh"
#include "G4Sphere.hh"
#include "G4Tubs.hh"
#include "G4Polycone.hh"
#include "G4ExtrudedSolid.hh"
#include "G4UnionSolid.hh"
#include "G4SubtractionSolid.hh"
#include "G4MultiUnion.hh"
#include "G4LogicalVolume.hh"
#include "G4PVPlacement.hh"
#include "G4Transform3D.hh"
#include "G4SystemOfUnits.hh"
#include "G4VisAttributes.hh"
#include "G4Colour.hh"
#include "G4PhysicalConstants.hh"
#include "G4UIcmdWith3VectorAndUnit.hh"
#include "G4UIcmdWithABool.hh"
#include "G4UIcmdWithAnInteger.hh"

namespace QArray
{
  void AddMultiUnionNode(G4MultiUnion *unionSolid,
                         G4VSolid *nodeSolid,
                         const G4Transform3D &transform)
  {
    G4Transform3D nodeTransform = transform;
    unionSolid->AddNode(*nodeSolid, nodeTransform);
  }

  // Initialize static member variables
  // G4int DetectorConstruction::fNDet1 = 0;
  // G4int DetectorConstruction::fNDet2 = 0;

  DetectorConstruction::DetectorConstruction()
      : G4VUserDetectorConstruction()
  {
    mNistManager = G4NistManager::Instance();
    DefineCommands();
  }

  DetectorConstruction::~DetectorConstruction()
  {
    delete mMessenger;
  }

  G4VPhysicalVolume *DetectorConstruction::Construct()
  {
    auto meta = Metadata::GetInstance();
    // Get nist material manager and materials
    G4Material *air = mNistManager->FindOrBuildMaterial("G4_AIR");

    // Option to switch on/off checking of volumes overlaps
    G4bool checkOverlaps = true;

    // World
    G4double world_sizeXYZ = 30.0 * m;

    auto worldSolid = new G4Box("worldBox",                                                     // its name
                                0.5 * world_sizeXYZ, 0.5 * world_sizeXYZ, 0.5 * world_sizeXYZ); // its size

    mWorldLogical = new G4LogicalVolume(worldSolid,      // its solid
                                        air,             // its material
                                        "worldLogical"); // its name

    auto worldPhysical = new G4PVPlacement(0,               // no rotation
                                           G4ThreeVector(), // at (0,0,0)
                                           mWorldLogical,   // its logical volume
                                           "worldPhysical", // its name
                                           nullptr,         // its mother volume
                                           false,           // no boolean operation
                                           0,               // copy number
                                           checkOverlaps);  // overlaps checking

    G4ThreeVector gOffset = meta->GetThreeVector("/QR/geom/globalOffset");

    G4bool bLab = meta->GetBool("/QR/geom/constructLab");
    if (bLab)
    {
      ConstructLab();
    }

    G4int iTableVer = meta->GetInt("/QR/geom/tableVer");
    G4bool bAddPb = meta->GetBool("/QR/geom/addPb");
    ConstructTable(iTableVer, bAddPb, gOffset);

    G4bool bFridge = meta->GetBool("/QR/geom/constructFridge");
    if (bFridge)
    {
      ConstructFridge();
      G4ThreeVector fridgePos = G4ThreeVector(-3.4 * m + 32 * cm + 1.05 * m / 2, 5.63 * m / 2 - (1.24 * m + 15 * cm + 1.15 * m / 2), 2520.0);
      new G4PVPlacement(0,
                        fridgePos + gOffset,
                        mFridgeLogical,
                        "fridgePhysical",
                        mWorldLogical,
                        false,
                        0,
                        checkOverlaps);
      AddExpPad(iTableVer);
    }

    // always return the physical World
    //
    return worldPhysical;
  }

  void DetectorConstruction::ConstructSDandField()
  {
    // Sensitive Detector
    SensitiveDetector *det1SD = new SensitiveDetector("scint_I");
    // det1SD->SetVerboseLevel(2);
    mDet1Logical->SetSensitiveDetector(det1SD);

    SensitiveDetector *det2SD = new SensitiveDetector("scint_II");
    mDet2Logical->SetSensitiveDetector(det2SD);

    SensitiveDetector *det3SD = new SensitiveDetector("scint_III");
    mDet3Logical->SetSensitiveDetector(det3SD);

    auto meta = Metadata::GetInstance();
    G4bool bFridge = meta->GetBool("/QR/geom/constructFridge");
    if (bFridge)
    {
      SensitiveDetector *chipSD = new SensitiveDetector("chip");
      mChipLogical->SetSensitiveDetector(chipSD);
    }
  }

  // Construct a hollow beam, dx, dy, dz are all outer diameters
  // t is the thickness of the wall
  // tr is for the inner box
  G4VSolid *ConstructHollowBox(G4String name,
                               G4double dx,
                               G4double dy,
                               G4double dz,
                               G4double t,
                               G4Transform3D tr = G4Transform3D::Identity)
  {
    G4Box *b1 = new G4Box(name + "_b1", dx / 2, dy / 2, dz / 2);
    G4Box *b2 = new G4Box(name + "_b2", dx / 2 - t, dy / 2 - t, dz / 2 - t);
    G4SubtractionSolid *s = new G4SubtractionSolid(name, b1, b2, tr);
    return s;
  }

  G4LogicalVolume *ConstructHollowBox(G4String name,
                                      G4Material *material,
                                      G4VisAttributes *vis,
                                      G4double dx,
                                      G4double dy,
                                      G4double dz,
                                      G4double t,
                                      G4Transform3D tr = G4Transform3D::Identity)
  {
    // Construct a hollow beam, dx, dy, dz are all outer diameters
    G4VSolid *s = ConstructHollowBox(name, dx, dy, dz, t, tr);
    G4LogicalVolume *l = new G4LogicalVolume(s, material, name + "Logical");
    l->SetVisAttributes(vis);
    return l;
  }

  // Construct the aluminum frame, the center is the frame center at floor level
  G4VSolid *ConstructFrame()
  {
    G4VSolid *vBeamBack = ConstructHollowBox("vBeamBack",
                                             15 * cm,
                                             15 * cm,
                                             2.54 * m - 15 * cm,
                                             1.25 * cm);
    G4VSolid *vBeamFront = ConstructHollowBox("vBeamFront",
                                              15 * cm,
                                              15 * cm,
                                              2.54 * m - 50 * cm,
                                              1.25 * cm);
    G4VSolid *hBeamBack = ConstructHollowBox("hBeamBack",
                                             6 * m,
                                             15 * cm,
                                             15 * cm,
                                             1.24 * cm);
    G4VSolid *hBeamFront = ConstructHollowBox("hBeamFront",
                                              6 * m,
                                              15 * cm,
                                              50 * cm,
                                              1.24 * cm);
    G4VSolid *hBeamLight = ConstructHollowBox("hBeamLight",
                                              25 * cm,
                                              1.15 * m,
                                              11 * cm,
                                              1.24 * cm);
    G4RotationMatrix r = G4RotationMatrix();
    G4MultiUnion *munion = new G4MultiUnion("frame");
    AddMultiUnionNode(munion, vBeamBack, G4Transform3D(r, G4ThreeVector(-2.925 * m, 1.30 * m / 2, (2.54 * m - 15 * cm) / 2)));
    AddMultiUnionNode(munion, vBeamBack, G4Transform3D(r, G4ThreeVector(-7.5 * cm, 1.30 * m / 2, (2.54 * m - 15 * cm) / 2)));
    AddMultiUnionNode(munion, vBeamBack, G4Transform3D(r, G4ThreeVector(7.5 * cm, 1.30 * m / 2, (2.54 * m - 15 * cm) / 2)));
    AddMultiUnionNode(munion, vBeamBack, G4Transform3D(r, G4ThreeVector(2.925 * m, 1.30 * m / 2, (2.54 * m - 15 * cm) / 2)));
    AddMultiUnionNode(munion, vBeamFront, G4Transform3D(r, G4ThreeVector(2.925 * m, -1.30 * m / 2, (2.54 * m - 50 * cm) / 2)));
    AddMultiUnionNode(munion, vBeamFront, G4Transform3D(r, G4ThreeVector(-2.925 * m, -1.30 * m / 2, (2.54 * m - 50 * cm) / 2)));
    AddMultiUnionNode(munion, hBeamBack, G4Transform3D(r, G4ThreeVector(0, 1.30 * m / 2, (2.54 * m - 7.5 * cm))));
    AddMultiUnionNode(munion, hBeamFront, G4Transform3D(r, G4ThreeVector(0, -1.30 * m / 2, (2.54 * m - 25 * cm))));
    AddMultiUnionNode(munion, hBeamLight, G4Transform3D(r, G4ThreeVector(-3.0 * m + 5 * cm + 12.5 * cm, 0, (2.54 * m - 13 * cm - 11 * cm / 2))));
    AddMultiUnionNode(munion, hBeamLight, G4Transform3D(r, G4ThreeVector(-3.0 * m + 5 * cm + 25 * cm + 1.05 * m + 12.5 * cm, 0, (2.54 * m - 13 * cm - 11 * cm / 2))));

    munion->Voxelize();
    return munion;
  }

  void DetectorConstruction::ConstructLab()
  {
    auto meta = Metadata::GetInstance();
    G4ThreeVector gOffset = meta->GetThreeVector("/QR/geom/globalOffset");

    // Define the concrete box
    G4Material *concrete = mNistManager->FindOrBuildMaterial("G4_CONCRETE");
    G4double labX = 7.8 * m, labY = 6.63 * m, labZ = 4.0 * m, labT = 0.5 * m;
    G4LogicalVolume *labLogical = ConstructHollowBox("lab", concrete, new G4VisAttributes(G4Colour(0.5, 0.5, 0.5, 1.0)), labX, labY, labZ, labT);
    G4ThreeVector labPos = G4ThreeVector(0, 0, (labZ - 2 * labT) / 2);
    new G4PVPlacement(0,
                      labPos + gOffset,
                      labLogical,
                      "labPhysical",
                      mWorldLogical,
                      false,
                      0,
                      true);

    // Define the catwalk
    G4Material *wood = mNistManager->FindOrBuildMaterial("G4_CELLULOSE_CELLOPHANE");
    G4double cwX = 6.79 * m, cwY = 1.24 * m, cwZ = 1.3 * m, cwT = 2 * cm;
    G4ThreeVector catwalkPos = G4ThreeVector(0, (labY - 2 * labT) / 2 - cwY / 2, cwZ / 2);
    G4LogicalVolume *catwalkLogical = ConstructHollowBox("catwalk", wood, new G4VisAttributes(G4Colour(244 / 255., 164 / 255., 96 / 255.)), cwX, cwY, cwZ, cwT);
    new G4PVPlacement(0,
                      catwalkPos + gOffset,
                      catwalkLogical,
                      "catwalkPhysical",
                      mWorldLogical,
                      false,
                      0,
                      true);

    // Define Aluminum beam
    G4Material *aluminum = mNistManager->FindOrBuildMaterial("G4_Al");
    G4VSolid *frame = ConstructFrame();
    G4Colour pewter = G4Colour(137 / 255., 148 / 255., 153 / 255.);
    G4LogicalVolume *frameLogical = new G4LogicalVolume(frame, aluminum, "frameLogical");
    frameLogical->SetVisAttributes(new G4VisAttributes(pewter));
    G4ThreeVector framePos = G4ThreeVector(-0.38 * m, (labY - 2 * labT) / 2 - (cwY + 15 * cm + 1.15 * m / 2), 0.0);
    new G4PVPlacement(0,
                      framePos + gOffset,
                      frameLogical,
                      "framePhysical",
                      mWorldLogical,
                      false,
                      0,
                      true);
  }

  // Construct the can with subtraction solid
  // Note the can is hollow
  // The center of the can is the center of the flange top
  G4VSolid *ConstructCan(G4String name,
                         G4double flangeR,
                         G4double flangeT,
                         G4double h,
                         G4double outerR,
                         G4double t,
                         G4double bottomT)
  {
    G4double hTotal = bottomT + h + 0.001 + flangeT;
    G4double zPlanes[] = {0., bottomT + h, bottomT + h + 0.001, bottomT + h + 0.001 + flangeT};
    for (int i = 0; i < 4; i++)
      zPlanes[i] -= hTotal;
    G4double rInner[] = {0., 0., 0., 0.};
    G4double rOuter[] = {outerR, outerR, flangeR, flangeR};
    G4Polycone *can = new G4Polycone(name + "_can",
                                     0.,
                                     twopi,
                                     4,
                                     zPlanes,
                                     rInner,
                                     rOuter);
    G4Tubs *vacuum = new G4Tubs(name + "_vac", 0, outerR - t, (h + flangeT) / 2, 0, twopi);
    G4Translate3D tr = G4Translate3D(0, 0, -(h + flangeT) / 2);
    G4SubtractionSolid *c = new G4SubtractionSolid(name, can, vacuum, tr);
    return c;
  }

  // Construct the fridge
  // The center of the fridge is the center of the OVC top
  void DetectorConstruction::ConstructFridge()
  {
    G4Material *aluminum = mNistManager->FindOrBuildMaterial("G4_Al");
    G4Material *copper = mNistManager->FindOrBuildMaterial("G4_Cu");
    G4Material *vac = mNistManager->FindOrBuildMaterial("G4_Galactic");

    // Colors
    G4Colour pewter = G4Colour(137 / 255., 148 / 255., 153 / 255.);
    G4Colour OVCColor = G4Colour(187 / 255., 0 / 255., 0 / 255.);
    G4Colour copperColor = G4Colour(184 / 255., 115 / 255., 51 / 255.);
    G4Colour vacuumColor = G4Colour(0, 0, 1.0, 0.1);

    // OVC top
    G4Box *stageOVCTop = new G4Box("stageOVCTop", 1500.0 / 2, 1000.0 / 2, 40.0 / 2);
    G4double flangeR = 690.0 / 2, flangeT = 16.0, h = 1350.0, outerR = 648.0 / 2, t = 5.0, bottomT = 20.0;
    G4double zPlanes[] = {0., bottomT + h, bottomT + h + 0.001, bottomT + h + 0.001 + flangeT};
    G4double rInner[] = {0., 0., 0., 0.};
    G4double rOuter[] = {outerR, outerR, flangeR, flangeR};
    G4Polycone *stageOVCCan = new G4Polycone("stageOVCCan",
                                             0.,
                                             twopi,
                                             4,
                                             zPlanes,
                                             rInner,
                                             rOuter);
    G4Tubs *stageOVCVac = new G4Tubs("stageOVCVac", 0, outerR - t, (h + flangeT) / 2, 0, twopi);
    G4MultiUnion *stageOVCSolid = new G4MultiUnion("stageOVCSolid");
    AddMultiUnionNode(stageOVCSolid, stageOVCTop, G4Transform3D(G4RotationMatrix(), G4ThreeVector(0, 0, 0)));
    AddMultiUnionNode(stageOVCSolid, stageOVCCan, G4Transform3D(G4RotationMatrix(), G4ThreeVector(0, 0, -(40.0 / 2 + bottomT + h + 0.001 + flangeT))));
    stageOVCSolid->Voxelize();

    G4SubtractionSolid *stageOVC = new G4SubtractionSolid("stageOVC", stageOVCSolid, stageOVCVac, 0, G4ThreeVector(0, 0, -(40.0 + h + flangeT) / 2));

    G4LogicalVolume *stageOVCLogical = mFridgeLogical = new G4LogicalVolume(stageOVC, aluminum, "stageOVCLogical");
    stageOVCLogical->SetVisAttributes(new G4VisAttributes(OVCColor));
    mStageOVCVacLogical = new G4LogicalVolume(stageOVCVac, vac, "stageOVCVacLogical");
    mStageOVCVacLogical->SetVisAttributes(new G4VisAttributes(vacuumColor));
    new G4PVPlacement(0,
                      G4ThreeVector(0, 0, -(40.0 + h + flangeT) / 2),
                      mStageOVCVacLogical,
                      "stageOVCVacPhysical",
                      stageOVCLogical,
                      false,
                      0,
                      true);
    // Note all daughter volumes should be under OVCVac

    // 50K stage
    G4Tubs *stage50KTop = new G4Tubs("stage50KTop", 0, 608.6 / 2, 19.0 / 2, 0, twopi);
    G4LogicalVolume *stage50KTopLogical = new G4LogicalVolume(stage50KTop, copper, "stage50KTopLogical");
    stage50KTopLogical->SetVisAttributes(new G4VisAttributes(copperColor));
    G4VSolid *stage50KCan = ConstructCan("stage50KCan",
                                         608.6 / 2,
                                         19.0,
                                         1169.7,
                                         579.0 / 2,
                                         5.0,
                                         10.0);
    G4LogicalVolume *stage50KCanLogical = new G4LogicalVolume(stage50KCan, aluminum, "stage50KCanLogical");
    stage50KCanLogical->SetVisAttributes(new G4VisAttributes(pewter));
    G4double stage50KZ = (h + flangeT) / 2 - 120.0 - 19.0 / 2;
    new G4PVPlacement(0,
                      G4ThreeVector(0, 0, stage50KZ),
                      stage50KTopLogical,
                      "stage50KTopPhysical",
                      mStageOVCVacLogical,
                      false,
                      0,
                      true);
    new G4PVPlacement(0,
                      G4ThreeVector(0, 0, stage50KZ - 19.0 / 2),
                      stage50KCanLogical,
                      "stage50KCanPhysical",
                      mStageOVCVacLogical,
                      false,
                      0,
                      true);

    // 4K stage
    G4Tubs *stage4KTop = new G4Tubs("stage4KTop", 0, 550.0 / 2, 19.0 / 2, 0, twopi);
    G4LogicalVolume *stage4KTopLogical = new G4LogicalVolume(stage4KTop, copper, "stage4KTopLogical");
    stage4KTopLogical->SetVisAttributes(new G4VisAttributes(copperColor));
    G4VSolid *stage4KCan = ConstructCan("stage4KCan",
                                        550.0 / 2,
                                        19.0,
                                        943.0,
                                        513.5 / 2,
                                        5.0,
                                        19.0);
    G4LogicalVolume *stage4KCanLogical = new G4LogicalVolume(stage4KCan, aluminum, "stage4KCanLogical");
    stage4KCanLogical->SetVisAttributes(new G4VisAttributes(pewter));
    G4double stage4KZ = stage50KZ - 19.0 / 2 - 120.0 - 19.0 / 2;
    new G4PVPlacement(0,
                      G4ThreeVector(0, 0, stage4KZ),
                      stage4KTopLogical,
                      "stage4KTopPhysical",
                      mStageOVCVacLogical,
                      false,
                      0,
                      true);
    new G4PVPlacement(0,
                      G4ThreeVector(0, 0, stage4KZ - 19.0 / 2),
                      stage4KCanLogical,
                      "stage4KCanPhysical",
                      mStageOVCVacLogical,
                      false,
                      0,
                      true);

    // 1K stage
    G4Tubs *stage1KTop = new G4Tubs("stage1KTop", 0, 457.5 / 2, 20.0 / 2, 0, twopi);
    G4LogicalVolume *stage1KTopLogical = new G4LogicalVolume(stage1KTop, copper, "stage1KTopLogical");
    stage1KTopLogical->SetVisAttributes(new G4VisAttributes(copperColor));
    G4VSolid *stage1KCan = ConstructCan("stage1KCan",
                                        457.5 / 2,
                                        2.0,
                                        804.0,
                                        454.5 / 2,
                                        1.0,
                                        1.0);
    G4LogicalVolume *stage1KCanLogical = new G4LogicalVolume(stage1KCan, copper, "stage1KCanLogical");
    stage1KCanLogical->SetVisAttributes(new G4VisAttributes(copperColor));
    G4double stage1KZ = stage4KZ - 19.0 / 2 - 120.0 - 20.0 / 2;
    new G4PVPlacement(0,
                      G4ThreeVector(0, 0, stage1KZ),
                      stage1KTopLogical,
                      "stage1KTopPhysical",
                      mStageOVCVacLogical,
                      false,
                      0,
                      true);
    new G4PVPlacement(0,
                      G4ThreeVector(0, 0, stage1KZ - 20.0 / 2),
                      stage1KCanLogical,
                      "stage1KCanPhysical",
                      mStageOVCVacLogical,
                      false,
                      0,
                      true);

    // CP stage
    G4Tubs *stageCPTop = new G4Tubs("stageCPTop", 0, 437.5 / 2, 20.0 / 2, 0, twopi);
    G4LogicalVolume *stageCPTopLogical = new G4LogicalVolume(stageCPTop, copper, "stageCPTopLogical");
    stageCPTopLogical->SetVisAttributes(new G4VisAttributes(copperColor));
    G4double stageCPZ = stage1KZ - 20.0 / 2 - 120.0 - 20.0 / 2;
    new G4PVPlacement(0,
                      G4ThreeVector(0, 0, stageCPZ),
                      stageCPTopLogical,
                      "stageCPTopPhysical",
                      mStageOVCVacLogical,
                      false,
                      0,
                      true);

    // MXC stage
    G4Tubs *stageMXCTop = new G4Tubs("stageMXCTop", 0, 407.5 / 2, 20.0 / 2, 0, twopi);
    G4LogicalVolume *stageMXCTopLogical = new G4LogicalVolume(stageMXCTop, copper, "stageMXCTopLogical");
    stageMXCTopLogical->SetVisAttributes(new G4VisAttributes(copperColor));
    fMXCStageZ = stageCPZ - 20.0 / 2 - 120.0 - 20.0 / 2;
    new G4PVPlacement(0,
                      G4ThreeVector(0, 0, fMXCStageZ),
                      stageMXCTopLogical,
                      "stageMXCTopPhysical",
                      mStageOVCVacLogical,
                      false,
                      0,
                      true);
  }

  void DetectorConstruction::ConstructTable(int iTableVer, bool bAddPb, G4ThreeVector gOffset)
  {
    // Note the table version determines the geometry version
    G4Material *wood = mNistManager->FindOrBuildMaterial("G4_CELLULOSE_CELLOPHANE");
    G4Material *air = mNistManager->FindOrBuildMaterial("G4_AIR");
    G4Material *scint = mNistManager->FindOrBuildMaterial("G4_PLASTIC_SC_VINYLTOLUENE");
    G4Material *lead = mNistManager->FindOrBuildMaterial("G4_Pb");
    G4Material *aluminum = mNistManager->FindOrBuildMaterial("G4_Al");

    G4Colour airColor = G4Colour(1.0, 1.0, 1.0);
    G4Colour detColor = G4Colour(156 / 255., 234 / 255., 233 / 255.);

    // Table
    G4double tableTopX = 91 * cm, tableTopY = 61 * cm, tableTopZ = 1.5 * cm;
    G4double tableX = 1.1 * m, tableY = 1.1 * m;
    G4double tableSpaceZ = 0.23 * m; //< space for holding detectors
    G4Box *table = new G4Box("table", tableX / 2, tableY / 2, (tableTopZ + tableSpaceZ) / 2);
    G4Box *tableSpace = new G4Box("tableSpace", tableX / 2, tableY / 2, tableSpaceZ / 2);
    G4Box *tableTop = new G4Box("tableTop", tableTopX / 2, tableTopY / 2, tableTopZ / 2);
    G4LogicalVolume *tableLogical = mTableLogical = new G4LogicalVolume(table, air, "tableLogical");
    tableLogical->SetVisAttributes(new G4VisAttributes(false));
    G4LogicalVolume *tableTopLogical = new G4LogicalVolume(tableTop, wood, "tableTopLogical");
    tableTopLogical->SetVisAttributes(new G4VisAttributes(G4Colour::Brown()));
    G4LogicalVolume *tableSpaceLogical = mTableSpaceLogical = new G4LogicalVolume(tableSpace, air, "tableSpaceLogical");
    tableSpaceLogical->SetVisAttributes(new G4VisAttributes(airColor));
    new G4PVPlacement(0,
                      G4ThreeVector(0, 0, tableTopZ / 2),
                      tableSpaceLogical,
                      "tableSpacePhysical",
                      tableLogical,
                      false,
                      0,
                      true);
    new G4PVPlacement(0,
                      G4ThreeVector(0, 0, -tableSpaceZ / 2),
                      tableTopLogical,
                      "tableTopPhysical",
                      tableLogical,
                      false,
                      0,
                      true);
    G4ThreeVector tabRef = G4ThreeVector(0, -tableTopY / 2, -tableSpaceZ / 2);

    // Box shape
    G4double Al_foil_t = 0.5 * mm;
    G4double det1_x = 51.0 * cm, det1_y = 7.2 * cm, det1_z = 2.0 * cm;
    G4double det2_x = 60.0 * cm, det2_y = 7.0 * cm, det2_z = 7.0 * cm;
    G4double det3_x = 1 * m, det3_y = 57.15, det3_z = 60.0;
    G4Box *det1Solid = new G4Box("det1Box",
                                 0.5 * det1_x - Al_foil_t, 0.5 * det1_y - Al_foil_t, 0.5 * det1_z - Al_foil_t);
    G4Box *det2Solid = new G4Box("det2Box",
                                 0.5 * det2_x - Al_foil_t, 0.5 * det2_y - Al_foil_t, 0.5 * det2_z - Al_foil_t);
    G4Box *det3Solid = new G4Box("det3Box",
                                 0.5 * det3_x - Al_foil_t, 0.5 * det3_y - Al_foil_t, 0.5 * det3_z - Al_foil_t);
    G4VSolid *det1Foil = ConstructHollowBox("det1Foil", det1_x, det1_y, det1_z, Al_foil_t);
    G4VSolid *det2Foil = ConstructHollowBox("det2Foil", det2_x, det2_y, det2_z, Al_foil_t);
    G4VSolid *det3Foil = ConstructHollowBox("det3Foil", det3_x, det3_y, det3_z, Al_foil_t);

    G4LogicalVolume *det1FoilLogical = new G4LogicalVolume(det1Foil,
                                                           aluminum,
                                                           "det1FoilLogical");
    det1FoilLogical->SetVisAttributes(new G4VisAttributes(G4Colour::Gray()));
    G4LogicalVolume *det2FoilLogical = new G4LogicalVolume(det2Foil,
                                                           aluminum,
                                                           "det2FoilLogical");
    det2FoilLogical->SetVisAttributes(new G4VisAttributes(G4Colour::Gray()));
    G4LogicalVolume *det3FoilLogical = new G4LogicalVolume(det3Foil,
                                                           aluminum,
                                                           "det3FoilLogical");
    det3FoilLogical->SetVisAttributes(new G4VisAttributes(G4Colour::Gray()));

    mDet1Logical = new G4LogicalVolume(det1Solid,
                                       scint,
                                       "det1Logical");
    mDet1Logical->SetVisAttributes(new G4VisAttributes(detColor));

    mDet2Logical = new G4LogicalVolume(det2Solid,
                                       scint,
                                       "det2Logical");
    mDet2Logical->SetVisAttributes(new G4VisAttributes(detColor));
    mDet3Logical = new G4LogicalVolume(det3Solid,
                                       scint,
                                       "det3Logical");
    mDet3Logical->SetVisAttributes(new G4VisAttributes(detColor));

    G4Colour PbColor = G4Colour(50 / 255., 155 / 255., 36 / 255.);
    if (iTableVer == 20230323)
    {
      G4ThreeVector tablePos = G4ThreeVector(-3.4 * m + 75 * cm, 5.63 * m / 2 - (1.24 * m + 1.25 * m), (tableSpaceZ + tableTopZ) / 2);
      G4ThreeVector tablePosOffset = G4ThreeVector(0.315 * m, -(17.2 + 47.0) + 61 * cm / 2, 866.0 - tableTopZ);
      G4RotationMatrix tableRot = G4RotationMatrix();
      tableRot.rotateZ(45 * deg);
      G4Transform3D tableTr = G4Translate3D(tablePos + gOffset) * G4Rotate3D(tableRot) * G4Translate3D(tablePosOffset);
      new G4PVPlacement(tableTr,
                        mTableLogical,
                        "tablePhysical",
                        mWorldLogical,
                        false,
                        0,
                        true);

      // Lead box
      if (bAddPb)
      {
        G4double Pb_x = 8 * 25.4, Pb_y = 4 * 25.4, Pb_z = 2 * 25.4;
        G4Box *PbBox = new G4Box("PbBox", Pb_x / 2, Pb_y / 2, Pb_z / 2);
        G4LogicalVolume *PbLogical = new G4LogicalVolume(PbBox, lead, "PbLogical");
        PbLogical->SetVisAttributes(new G4VisAttributes(PbColor));

        // Reference is the center of the right most Pb column
        G4ThreeVector PbRef = tabRef + G4ThreeVector(32 * cm - Pb_x / 2, 36.5 * cm, Pb_z / 2);
        for (G4int iPbRow = 0; iPbRow <= 1; iPbRow++)
        {
          // Make a Pb array
          for (G4int iPb = 0; iPb <= 2; iPb++)
          {
            new G4PVPlacement(0,
                              PbRef + G4ThreeVector(-iPb * Pb_x, (2 * iPbRow + 1) * Pb_y / 2, 0),
                              PbLogical,
                              "PbPhysical",
                              tableSpaceLogical,
                              false,
                              1000 + iPbRow * 10 + iPb,
                              true);
            new G4PVPlacement(0,
                              PbRef + G4ThreeVector(-iPb * Pb_x, -(2 * iPbRow + 1) * Pb_y / 2, 0),
                              PbLogical,
                              "PbPhysical",
                              tableSpaceLogical,
                              false,
                              1000 - iPbRow * 10 - iPb,
                              true);
          }
        }
        // Level 2
        for (G4int iPb = 0; iPb <= 2; iPb++)
        {
          new G4PVPlacement(0,
                            PbRef + G4ThreeVector(-iPb * Pb_x, det2_y / 2 + Pb_y / 2, Pb_z),
                            PbLogical,
                            "PbPhysical",
                            tableSpaceLogical,
                            false,
                            1000 + 100 + iPb,
                            true);
          new G4PVPlacement(0,
                            PbRef + G4ThreeVector(-iPb * Pb_x, -det2_y / 2 - Pb_y / 2, Pb_z),
                            PbLogical,
                            "PbPhysical",
                            tableSpaceLogical,
                            false,
                            1000 - 100 - iPb,
                            true);
        }
        // Level 3
        for (G4int iPb = 0; iPb <= 2; iPb++)
        {
          new G4PVPlacement(new G4RotationMatrix(0, 90 * deg, 0),
                            PbRef + G4ThreeVector(-iPb * Pb_x, det1_y / 2 + Pb_z / 2, 1.5 * Pb_z + Pb_y / 2),
                            PbLogical,
                            "PbPhysical",
                            tableSpaceLogical,
                            false,
                            1000 + 200 + iPb,
                            true);
          new G4PVPlacement(new G4RotationMatrix(0, 90 * deg, 0),
                            PbRef + G4ThreeVector(-iPb * Pb_x, -det1_y / 2 - Pb_z / 2, 1.5 * Pb_z + Pb_y / 2),
                            PbLogical,
                            "PbPhysical",
                            tableSpaceLogical,
                            false,
                            1000 - 200 - iPb,
                            true);
        }
      }
      G4ThreeVector detDPos = tabRef + G4ThreeVector(349.05 - (det2_x / 2), 36.5 * cm, 2 * 25.4 + (det2_z / 2));
      G4ThreeVector detCPos = detDPos + G4ThreeVector(0, 0, det2_z + 0.25 * 25.4);
      G4ThreeVector detBPos = detCPos + G4ThreeVector(det2_x / 2 - det1_x / 2, 0, det2_z / 2 + 0.125 * 25.4 + det1_z / 2);
      G4ThreeVector detAPos = tabRef + G4ThreeVector(325 + det1_y / 2, det1_x / 2, det1_z / 2);
      new G4PVPlacement(0,                 // no rotation
                        detDPos,           // at position
                        mDet2Logical,      // its logical volume
                        "detDPhysical",    // its name
                        tableSpaceLogical, // its mother volume
                        false,             // no boolean operation
                        1,                 // copy number
                        true);             // overlaps checking
      // fNDet2++;

      new G4PVPlacement(0,
                        detDPos,
                        det2FoilLogical,
                        "detDFoil",
                        tableSpaceLogical,
                        false,
                        1,
                        true);

      new G4PVPlacement(0,                 // no rotation
                        detCPos,           // at position
                        mDet2Logical,      // its logical volume
                        "detCPhysical",    // its name
                        tableSpaceLogical, // its mother volume
                        false,             // no boolean operation
                        0,                 // copy number
                        true);             // overlaps checking
      // fNDet2++;

      new G4PVPlacement(0,
                        detCPos,
                        det2FoilLogical,
                        "detCFoil",
                        tableSpaceLogical,
                        false,
                        1,
                        true);

      new G4PVPlacement(0,                 // no rotation
                        detBPos,           // at position
                        mDet1Logical,      // its logical volume
                        "detBPhysical",    // its name
                        tableSpaceLogical, // its mother volume
                        false,             // no boolean operation
                        1,                 // copy number
                        true);             // overlaps checking
      // fNDet1++;

      new G4PVPlacement(0,
                        detBPos,
                        det1FoilLogical,
                        "detBFoil",
                        tableSpaceLogical,
                        false,
                        1,
                        true);

      new G4PVPlacement(new G4RotationMatrix(0, 0, 90 * degree),
                        detAPos,           // at position
                        mDet1Logical,      // its logical volume
                        "detAPhysical",    // its name
                        tableSpaceLogical, // its mother volume
                        false,             // no boolean operation
                        0,                 // copy number
                        true);             // overlaps checking
      // fNDet1++;

      new G4PVPlacement(new G4RotationMatrix(0, 0, 90 * degree),
                        detAPos,
                        det1FoilLogical,
                        "detAFoil",
                        tableSpaceLogical,
                        false,
                        1,
                        true);
    }
    else if (iTableVer == 20230601)
    {
      G4ThreeVector tablePos = G4ThreeVector(-3.4 * m + 75 * cm, 5.63 * m / 2 - (1.24 * m + 1.25 * m), (tableSpaceZ + tableTopZ) / 2);
      G4ThreeVector tablePosOffset = G4ThreeVector(0.315 * m, -65.0 + 61 * cm / 2, 866.0 - tableTopZ);
      G4RotationMatrix tableRot = G4RotationMatrix();
      tableRot.rotateZ(45 * deg);
      G4Transform3D tableTr = G4Translate3D(tablePos + gOffset) * G4Rotate3D(tableRot) * G4Translate3D(tablePosOffset);
      new G4PVPlacement(tableTr,
                        mTableLogical,
                        "tablePhysical",
                        mWorldLogical,
                        false,
                        0,
                        true);

      // Lead box
      if (bAddPb)
      {
        G4double Pb_x = 8 * 25.4, Pb_y = 4 * 25.4, Pb_z = 2 * 25.4;
        G4Box *PbBox = new G4Box("PbBox", Pb_x / 2, Pb_y / 2, Pb_z / 2);
        G4LogicalVolume *PbLogical = new G4LogicalVolume(PbBox, lead, "PbLogical");
        PbLogical->SetVisAttributes(new G4VisAttributes(PbColor));

        // Reference is the center of the right most Pb column
        G4ThreeVector PbRef = tabRef + G4ThreeVector(32 * cm - Pb_x / 2, 36.5 * cm, Pb_z / 2);
        for (G4int iPbRow = 0; iPbRow <= 1; iPbRow++)
        {
          // Make a Pb array
          for (G4int iPb = 0; iPb <= 2; iPb++)
          {
            new G4PVPlacement(0,
                              PbRef + G4ThreeVector(-iPb * Pb_x, (2 * iPbRow + 1) * Pb_y / 2, 0),
                              PbLogical,
                              "PbPhysical",
                              tableSpaceLogical,
                              false,
                              1000 + iPbRow * 10 + iPb,
                              true);
            new G4PVPlacement(0,
                              PbRef + G4ThreeVector(-iPb * Pb_x, -(2 * iPbRow + 1) * Pb_y / 2, 0),
                              PbLogical,
                              "PbPhysical",
                              tableSpaceLogical,
                              false,
                              1000 - iPbRow * 10 - iPb,
                              true);
          }
        }
        // Level 2
        for (G4int iPb = 0; iPb <= 2; iPb++)
        {
          new G4PVPlacement(0,
                            PbRef + G4ThreeVector(-iPb * Pb_x, det2_y / 2 + Pb_y / 2, Pb_z),
                            PbLogical,
                            "PbPhysical",
                            tableSpaceLogical,
                            false,
                            1000 + 100 + iPb,
                            true);
          new G4PVPlacement(0,
                            PbRef + G4ThreeVector(-iPb * Pb_x, -det2_y / 2 - Pb_y / 2 - det3_y, Pb_z),
                            PbLogical,
                            "PbPhysical",
                            tableSpaceLogical,
                            false,
                            1000 - 100 - iPb,
                            true);
        }
        // Level 3
        for (G4int iPb = 0; iPb <= 2; iPb++)
        {
          new G4PVPlacement(new G4RotationMatrix(0, 90 * deg, 0),
                            PbRef + G4ThreeVector(-iPb * Pb_x, det1_y / 2 + Pb_z / 2, 1.5 * Pb_z + Pb_y / 2),
                            PbLogical,
                            "PbPhysical",
                            tableSpaceLogical,
                            false,
                            1000 + 200 + iPb,
                            true);
          new G4PVPlacement(new G4RotationMatrix(0, 90 * deg, 0),
                            PbRef + G4ThreeVector(-iPb * Pb_x, -det1_y / 2 - Pb_z / 2 - det3_y, 1.5 * Pb_z + Pb_y / 2),
                            PbLogical,
                            "PbPhysical",
                            tableSpaceLogical,
                            false,
                            1000 - 200 - iPb,
                            true);
        }
      }
      // G4ThreeVector detDPos = tabRef + G4ThreeVector(349.05 - (det2_x / 2), 14.25 * 25.4, 2 * 25.4 + (det2_z / 2));
      G4ThreeVector detDPos = tabRef + G4ThreeVector(349.05 - (det2_x / 2), 36.5 * cm, 2 * 25.4 + (det2_z / 2));
      G4ThreeVector detCPos = detDPos + G4ThreeVector(0, 0, det2_z + 0.25 * 25.4);
      G4ThreeVector detBPos = detCPos + G4ThreeVector(det2_x / 2 - det1_x / 2, 0, det2_z / 2 + 0.125 * 25.4 + det1_z / 2);
      G4ThreeVector detFPos = detDPos + G4ThreeVector(det2_x / 2 - 24.0 + 195.0 - det3_x / 2, -det2_y / 2 - det3_y / 2, -det2_z / 2 + det3_z / 2);
      G4ThreeVector detEPos = detFPos + G4ThreeVector(0, 0, det3_z + 15.0);
      G4ThreeVector detAPos = detBPos + G4ThreeVector(0, -det1_y, 2.9);
      new G4PVPlacement(0,                 // no rotation
                        detDPos,           // at position
                        mDet2Logical,      // its logical volume
                        "detDPhysical",    // its name
                        tableSpaceLogical, // its mother volume
                        false,             // no boolean operation
                        1,                 // copy number
                        true);             // overlaps checking
      new G4PVPlacement(0,
                        detDPos,
                        det2FoilLogical,
                        "detDFoil",
                        tableSpaceLogical,
                        false,
                        1,
                        true);
      new G4PVPlacement(0,                 // no rotation
                        detCPos,           // at position
                        mDet2Logical,      // its logical volume
                        "detCPhysical",    // its name
                        tableSpaceLogical, // its mother volume
                        false,             // no boolean operation
                        0,                 // copy number
                        true);             // overlaps checking
      new G4PVPlacement(0,
                        detCPos,
                        det2FoilLogical,
                        "detCFoil",
                        tableSpaceLogical,
                        false,
                        1,
                        true);
      new G4PVPlacement(0,                 // no rotation
                        detBPos,           // at position
                        mDet1Logical,      // its logical volume
                        "detBPhysical",    // its name
                        tableSpaceLogical, // its mother volume
                        false,             // no boolean operation
                        1,                 // copy number
                        true);             // overlaps checking
      new G4PVPlacement(0,
                        detBPos,
                        det1FoilLogical,
                        "detBFoil",
                        tableSpaceLogical,
                        false,
                        1,
                        true);
      new G4PVPlacement(0,
                        detAPos,           // at position
                        mDet1Logical,      // its logical volume
                        "detAPhysical",    // its name
                        tableSpaceLogical, // its mother volume
                        false,             // no boolean operation
                        0,                 // copy number
                        true);             // overlaps checking
      new G4PVPlacement(0,
                        detAPos,
                        det1FoilLogical,
                        "detAFoil",
                        tableSpaceLogical,
                        false,
                        1,
                        true);
      new G4PVPlacement(0,
                        detFPos,           // at position
                        mDet3Logical,      // its logical volume
                        "detFPhysical",    // its name
                        tableSpaceLogical, // its mother volume
                        false,             // no boolean operation
                        0,                 // copy number
                        true);             // overlaps checking
      new G4PVPlacement(0,
                        detFPos,
                        det3FoilLogical,
                        "detFFoil",
                        tableSpaceLogical,
                        false,
                        1,
                        true);
      new G4PVPlacement(0,
                        detEPos,           // at position
                        mDet3Logical,      // its logical volume
                        "detEPhysical",    // its name
                        tableSpaceLogical, // its mother volume
                        false,             // no boolean operation
                        1,                 // copy number
                        true);             // overlaps checking
      new G4PVPlacement(0,
                        detEPos,
                        det3FoilLogical,
                        "detEFoil",
                        tableSpaceLogical,
                        false,
                        1,
                        true);

      // G4cout << "Det A pos: " << (detAPos + tableTr.getTranslation()) / m << G4endl;
      // G4cout << "Det B pos: " << (detBPos + tableTr.getTranslation()) / m << G4endl;
      // G4cout << "Det C pos: " << (detCPos + tableTr.getTranslation()) / m << G4endl;
      // G4cout << "Det D pos: " << (detDPos + tableTr.getTranslation()) / m << G4endl;
      // G4cout << "Det E pos: " << (detEPos + tableTr.getTranslation()) / m << G4endl;
      // G4cout << "Det F pos: " << (detFPos + tableTr.getTranslation()) / m << G4endl;
    }
    else
    {
      G4Exception("DetectorConstruction::ConstructTable", "InvalidTableVersion", FatalException, "Invalid table version.");
    }
  }

  void DetectorConstruction::AddExpPad(int iTableVer)
  {
    G4Material *copper = mNistManager->FindOrBuildMaterial("G4_Cu");
    G4Material *silicon = mNistManager->FindOrBuildMaterial("G4_Si");

    G4Colour copperPadColor = G4Colour(220 / 255., 180 / 255., 135 / 255.);
    G4Colour chipBoxColor = G4Colour(255 / 255., 215 / 255., 0 / 255.);

    // Adds the experimental pad in the 1K stage
    G4double pad_x = 6.30, pad_y = 224.15, pad_z = 405.0;
    G4Box *padBox = new G4Box("padBox", 0.5 * pad_x, 0.5 * pad_y, 0.5 * pad_z);
    G4LogicalVolume *padLogical = new G4LogicalVolume(padBox, copper, "padLogical");
    padLogical->SetVisAttributes(new G4VisAttributes(copperPadColor));
    G4double fPadZ = fMXCStageZ - 20.0 / 2 - 0.5 * pad_z;
    G4double fPadX = -101.6;
    G4RotationMatrix rot = G4RotationMatrix();
    rot.rotateZ(45 * deg);
    G4Transform3D tr = G4Rotate3D(rot) * G4Translate3D(fPadX, 0, fPadZ);
    new G4PVPlacement(tr,
                      padLogical,
                      "padPhysical",
                      mStageOVCVacLogical,
                      false,
                      0,
                      true);

    G4Colour violet = G4Colour(127 / 255., 0 / 255., 255 / 255.);

    if (iTableVer == 20230323)
    {
      // Add the chip enclosing box
      G4double chipBox_x = 15.0, chipBox_y = 52.18, chipBox_z = 52.18;
      G4Box *chipBox = new G4Box("chipBox", 0.5 * chipBox_x, 0.5 * chipBox_y, 0.5 * chipBox_z);
      G4LogicalVolume *chipBoxLogical = new G4LogicalVolume(chipBox, copper, "chipBoxLogical");
      chipBoxLogical->SetVisAttributes(new G4VisAttributes(chipBoxColor));
      G4double fChipBoxZ = fPadZ - 0.5 * pad_z + 35.15 + 0.5 * chipBox_z;
      G4double fChipBoxX = fPadX - 0.5 * pad_x - 0.5 * chipBox_x;
      G4Transform3D chipBoxTr = G4Rotate3D(rot) * G4Translate3D(fChipBoxX, 0, fChipBoxZ);
      new G4PVPlacement(chipBoxTr,
                        chipBoxLogical,
                        "chipBoxPhysical",
                        mStageOVCVacLogical,
                        false,
                        0,
                        true);

      // Add the chip
      G4double chip_x = 10.15, chip_y = 350 * um, chip_z = 10.15;
      G4Box *chip = new G4Box("chip", 0.5 * chip_x, 0.5 * chip_y, 0.5 * chip_z);
      mChipLogical = new G4LogicalVolume(chip, silicon, "chipLogical");
      mChipLogical->SetVisAttributes(new G4VisAttributes(violet));
      G4RotationMatrix chipRot = G4RotationMatrix();
      chipRot.rotateY(45 * deg);
      chipRot.rotateZ(90 * deg);
      G4Transform3D chipTr = G4Translate3D(chipBox_x / 2 - 10.60, 0, 0) * G4Rotate3D(chipRot);
      new G4PVPlacement(chipTr,
                        mChipLogical,
                        "detQPhysical",
                        chipBoxLogical,
                        false,
                        0,
                        true);
    }
    else if (iTableVer == 20230601)
    {
      // Add the chip enclosing box
      G4double chipBox_x = 13.0, chipBox_y = 52.18, chipBox_z = 52.18;
      G4Box *chipBox = new G4Box("chipBox", 0.5 * chipBox_x, 0.5 * chipBox_y, 0.5 * chipBox_z);
      G4LogicalVolume *chipBoxLogical = new G4LogicalVolume(chipBox, copper, "chipBoxLogical");
      chipBoxLogical->SetVisAttributes(new G4VisAttributes(chipBoxColor));
      G4double fChipBoxZ = fPadZ - 0.5 * pad_z + 35.15 + 0.5 * chipBox_z + 2 * 25.4;
      G4double fChipBoxX = fPadX - 0.5 * pad_x - 0.5 * chipBox_x;
      G4Transform3D chipBoxTr = G4Rotate3D(rot) * G4Translate3D(fChipBoxX, 0, fChipBoxZ);
      new G4PVPlacement(chipBoxTr,
                        chipBoxLogical,
                        "chipBoxPhysical",
                        mStageOVCVacLogical,
                        false,
                        0,
                        true);

      // Add the chip
      G4double chip_x = 350 * um, chip_y = 5.0, chip_z = 5.0;
      G4Box *chip = new G4Box("chip", 0.5 * chip_x, 0.5 * chip_y, 0.5 * chip_z);
      mChipLogical = new G4LogicalVolume(chip, silicon, "chipLogical");
      mChipLogical->SetVisAttributes(new G4VisAttributes(violet));
      G4Transform3D chipTr = G4Translate3D(chipBox_x / 2 - 10.60 + 4.17, 0, 0);
      new G4PVPlacement(chipTr,
                        mChipLogical,
                        "detQPhysical",
                        chipBoxLogical,
                        false,
                        0,
                        true);
    }
    else
    {
      G4Exception("DetectorConstruction::ConstructTable", "InvalidTableVersion", FatalException, "Invalid table version.");
    }
  }

  // void DetectorConstruction::AddScintI(G4String name)
  // {
  //   // Check if the scintillator name is already added
  //   if (std::find(fDetNames.begin(), fDetNames.end(), name) != fDetNames.end())
  //   {
  //     G4Exception("DetectorConstruction::AddScintI", "ScintillatorExists", JustWarning, "Scintillator already exists.");
  //   }
  //   else
  //   {
  //     // Add the scintillator name to the list
  //     fDetNames.push_back(name);
  //   }
  // }

  // void DetectorConstruction::AddScintII(G4String name)
  // {
  // }

  void DetectorConstruction::DefineCommands()
  {
    // Define commands
    mMessenger = new G4GenericMessenger(this,
                                        "/QR/geom/",
                                        "Geometry control");

    auto meta = Metadata::GetInstance();
    auto *gOffsetCmd = meta->AddParamCommand<G4UIcmdWith3VectorAndUnit>(
        "/QR/geom/globalOffset", "Global offset for everything.");
    gOffsetCmd->SetUnitCategory("Length");
    meta->Set("/QR/geom/globalOffset", G4ThreeVector(0, 0, 0));

    meta->AddParamCommand<G4UIcmdWithAnInteger>("/QR/geom/tableVer",
                                                "Geometry or table version",
                                                "tableVer");
    meta->Set("/QR/geom/tableVer", 20230323);

    meta->AddParamCommand<G4UIcmdWithABool>("/QR/geom/constructLab",
                                            "Construct Lab?",
                                            "cLab");
    meta->Set("/QR/geom/constructLab", true);

    meta->AddParamCommand<G4UIcmdWithABool>("/QR/geom/constructFridge",
                                            "Construct Fridge?",
                                            "cFridge");
    meta->Set("/QR/geom/constructFridge", true);

    meta->AddParamCommand<G4UIcmdWithABool>("/QR/geom/addPb",
                                            "Add Pb?",
                                            "addPb");
    meta->Set("/QR/geom/addPb", true);
  }
}
