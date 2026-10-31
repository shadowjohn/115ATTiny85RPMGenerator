// Reuse the existing real-register simulator, not a second implementation of the sketch.
#define HOST_SKETCH_V3 1
#define main legacy_regression_main
#include "regression.cpp"
#undef main

static void pressV3() { driveButton(true, 50000); driveButton(false, 50000); }
static void modesV3() {
  sim::adc = 1023; setup(); foregroundFor(900000);
  require(mode == MODE_STEPS && rpm == 14000 && sim::displayedNumber == 1400, "startup mode/RPM display");
  driveButton(true, 10000); driveButton(false, 50000);
  require(mode == MODE_STEPS, "short bounce changed mode");
  driveButton(true, 60000);
  require(mode == MODE_SMOOTH && sim::displayedNumber == 2, "first press must show mode 2");
  foregroundFor(900000);
  require(mode == MODE_SMOOTH && sim::displayedNumber == 1400, "held press repeated or hint did not expire");
  driveButton(false,50000);pressV3();
  require(mode == MODE_AUTO && rpm == 0 && sim::displayedNumber == 3, "second press must show mode 3 and start zero");
  pressV3();
  require(mode == MODE_STEPS && rpm == 14000 && sim::displayedNumber == 1, "third press must return current VR and show 1");
  foregroundFor(900000);
  require(sim::displayedNumber == 1400, "RPM not restored after mode 1 hint");
}
static void mappingV3() {
  require(smoothRPMForADC(0)==0 && smoothRPMForADC(8)==0, "zero endpoint plateau");
  require(smoothRPMForADC(9)==14, "first nonzero ADC must not clamp to 500 RPM");
  require(smoothRPMForADC(512)==7007, "midpoint linear mapping");
  require(smoothRPMForADC(1014)==13986 && smoothRPMForADC(1015)==14000 && smoothRPMForADC(1023)==14000, "full endpoint plateau");
  int previous=-1, distinct=0;
  for(int adc=0;adc<=1023;++adc) {
    const int value=smoothRPMForADC(adc);
    require(value>=previous && value<=14000,"mapping not monotonic/bounded");
    if(value!=previous)++distinct;
    previous=value;
  }
  require(distinct>1000,"smooth mode was quantized into coarse steps");
  setup();sim::adc=512;foregroundFor(200000);pressV3();foregroundFor(200000);
  require(mode==MODE_SMOOTH && rpm==7007,"loop failed to use smooth mapping");
  sim::adc=0;foregroundFor(200000);
  require(rpm==0 && !(uint8_t(PORTB)&_BV(PB4)),"smooth zero must stop output");
  sim::adc=1023;foregroundFor(200000);
  require(rpm==14000,"smooth full-scale response");
}
static void lowPulseV3(int target) {
  setup();modeDisplayActive=false;updateRPM(target);
  const uint64_t period=60000000ULL/target;
  foregroundFor(period*4+10000,true);
  const auto rises=risingEdges();require(rises.size()>=3,"low RPM missing pulses");
  for(size_t i=1;i<rises.size();++i)closeTo(double(rises[i]-rises[i-1]),60000000.0/target,"low RPM overflow/timing");
  for(size_t i=1;i<sim::edges.size();++i)
    closeTo(double(sim::edges[i].at-sim::edges[i-1].at),(60000000.0/target)*(sim::edges[i-1].high?11.0/12.0:1.0/12.0),"low RPM duty cycle");
  updateRPM(0);require(!pulseRunning && !(uint8_t(PORTB)&_BV(PB4)),"low RPM stop");
}
static void accelerateV3() {
  setup();updateRPM(14);foregroundFor(500000,true,true);
  sim::edges.clear();updateRPM(14000);foregroundFor(20000,true,true);
  require(risingEdges().size()>=3,"fast twist stuck behind several-second low RPM cycle");
}
static void autoV3() {
  sim::adc=1023;setup();foregroundFor(100000);pressV3();pressV3();
  require(mode==MODE_AUTO && rpm==0,"auto entry is not mode 3");
  foregroundFor(1500000);require(rpm==0,"initial zero hold ended early");
  waitForRPM(14000,700);foregroundFor(1500000);require(rpm==14000,"upper hold ended early");
  waitForRPM(13000,700);
  for(int r=12000;r>=0;r-=1000) { sim::adc=0;waitForRPM(r,1100); }
  foregroundFor(1500000);require(rpm==0,"bottom hold ended early");
  waitForRPM(1000,700);
  for(int r=2000;r<=14000;r+=1000)waitForRPM(r,1100);
  waitForRPM(13000,2100);pressV3();require(mode==MODE_STEPS && rpm==0,"auto exit must restore current zero VR");
}
static void wrapV3() {
  sim::nowUs=uint64_t(UINT32_MAX-500)*1000;sim::adc=1023;setup();foregroundFor(100000);pressV3();
  require(sim::displayedNumber==2,"mode hint before wrap");
  foregroundFor(600000);require(sim::displayedNumber==2,"mode hint expired early across wrap");
  foregroundFor(300000);require(sim::displayedNumber==1400,"mode hint stuck across wrap");
}
static void bootHeldV3() {
  sim::buttonGrounded=true;setup();foregroundFor(1000000);
  require(mode==MODE_STEPS,"boot-held switched mode");
  driveButton(false,50000);pressV3();require(mode==MODE_SMOOTH,"release/press did not switch");
}
int main(int argc,char**argv) {
  if(argc!=2)return 2;
  try {
    const std::string name=argv[1];
    if(name=="modes")modesV3();else if(name=="mapping")mappingV3();
    else if(name=="low_14")lowPulseV3(14);else if(name=="low_50")lowPulseV3(50);
    else if(name=="accelerate")accelerateV3();else if(name=="auto")autoV3();
    else if(name=="wrap")wrapV3();else if(name=="boot_held")bootHeldV3();
    else return legacy_regression_main(argc,argv);
    std::printf("PASS %s\n",argv[1]);return 0;
  }catch(const std::exception&e){std::printf("FAIL %s: %s\n",argv[1],e.what());return 1;}
}
