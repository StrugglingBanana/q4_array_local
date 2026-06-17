#ifndef QRPrimaryGeneratorAction_h
#define QRPrimaryGeneratorAction_h 1

#include "MCSampler.hh"

#include "G4VUserPrimaryGeneratorAction.hh"
#include "G4GeneralParticleSource.hh"
#include "G4ParticleGun.hh"
#include "G4GenericMessenger.hh"
#include "globals.hh"
#include "G4Event.hh"
#include "Randomize.hh"
#include "G4UnitsTable.hh"
#include "G4SystemOfUnits.hh"

#include <vector>
#include <unordered_map>
#include <random>

#ifdef QR_WITH_CRY
class CRYGenerator;
class CRYParticle;
#endif

namespace QArray
{
  class PrimaryGeneratorAction : public G4VUserPrimaryGeneratorAction
  {
  public:
    enum MODE
    {
      kParticleGun = 0,
      kGPS,
      kCRY,
      kVolumeScan
    };

    std::unordered_map<std::string, MODE> mModeMap = {
        {"particleGun", kParticleGun},
        {"gps", kGPS},
        {"volumeScan", kVolumeScan},
#ifdef QR_WITH_CRY
        {"cry", kCRY}
#endif
    };

    PrimaryGeneratorAction();
    virtual ~PrimaryGeneratorAction() override;

    // method from the base class
    virtual void GeneratePrimaries(G4Event *) override;

    G4GeneralParticleSource *GetGeneralParticleSource() const
    {
      return mParticleSource;
    };

    G4ParticleGun *GetParticleGun() const
    {
      return mParticleGun;
    };

    void BeginOfRunAction();
    void EndOfRunAction();

  private:
    void DefineCommands();

#ifdef QR_WITH_CRY
    void GenerateCRYEvent(G4Event *);
#endif

    // Generate the energy for the particle following a predetermined distribution.
    // The predetermined distribution is Gaisser parametrization at 0 zenith angle.
    G4double GenerateEnergy();
    G4double GeneratePhi();

    // Generate the theta and energy pair from MCSampler.
    void GenerateMCThetaEnergy(G4double &theta, G4double &energy);
    void DeInitSampler();

    // Generate the direction and position of the particle on a hemisphere
    // for the particle gun
    void InitCosmicParticleGun();
    void InitVolumeScan();
    void GenerateVolumeScanEvent(G4Event*);

    G4GenericMessenger *mMessenger = nullptr;

    G4GeneralParticleSource *mParticleSource = nullptr;
    G4ParticleGun *mParticleGun = nullptr;
    std::vector<G4ThreeVector> mVolumeScanPositions;
    G4RandGeneral *mRandGeneral = nullptr;

    std::default_random_engine mEngineC;
    std::discrete_distribution<int> mDiscDistC = std::discrete_distribution<int>({128, 100});

    MCSampler *mSampler = nullptr;

    MODE mMode = kParticleGun;
#ifdef QR_WITH_CRY
    CRYGenerator *mCRYGen = nullptr;                // CRY cosmic particle generator
    std::vector<CRYParticle *> vecCRYParts; // hold particles from CRY
    G4ThreeVector mCRYOffset;             // offset of CRY surface since it's not built-into CRY
#endif
    G4bool bApplyAcceptance = true;       // apply acceptance cuts to CRY particles
    G4ThreeVector mAcceptanceTarget;
    G4double fAcceptanceConeRsq;

    // User command variables
    G4double fSideL = 1.0 * m;
    G4double fThetaMin = 0.0;
    G4double fThetaMax = 90.0 * deg;
    G4double fMinEnergy = 0.5 * GeV;
    G4double fMaxEnergy = 1000.0 * GeV;

    G4double mCharge = 0;
  };
}

#endif
