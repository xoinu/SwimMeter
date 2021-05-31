#include <Arduino.h>
#include <LiquidCrystal.h>
#include <TactSwitch.h>

const int lapTargetCount = 7;
const uint32_t lapTargets[lapTargetCount] = {
    60000,
    72000,
    90000,
    100000,
    120000,
    180000,
    29033 // World record of 1500m free style: 14m31.02s
};
const uint32_t lapMax = 64;

enum AppStatus
{
  kAppStatusInit,
  kAppStatusRunning,
  kAppStatusPaused,
  kAppStatusConfirmReset
};

struct App
{
  uint32_t startTick;
  uint32_t pauseTick;
  uint32_t pauseTotal;
  uint32_t lapTick;
  uint32_t lapCount;
  uint32_t laps[lapMax];
  uint32_t total;
  uint32_t totalTarget;
  uint32_t displayTick;
  uint32_t lcdLedTick;
  int8_t status;
  int8_t lapTargetIdx;

  App() : lapTargetIdx(0), lcdLedTick(1)
  {
  }

  void init()
  {
    startTick = 0;
    pauseTick = 0;
    pauseTotal = 0;
    lapTick = 0;
    lapCount = 0;
    total = 0;
    totalTarget = 0;
    displayTick = 0;
    //lcdLedTick = 0;
    status = kAppStatusInit;
    //lapTargetIdx = 0;
    memcpy(laps, 0, sizeof(laps));
  }

  uint32_t appTick(uint32_t tick) const
  {
    switch (status)
    {
    case kAppStatusRunning:
      return tick - startTick - pauseTotal;
    case kAppStatusPaused:
    case kAppStatusConfirmReset:
      return pauseTick - startTick - pauseTotal;
    default:
      return 0;
    }
  }
};

static App app;
static void LF_onPush(uint32_t tick);
static void LF_onRelease(uint32_t tick);
static void LF_onHold(uint32_t tick);

LiquidCrystal lcd(5, 4, 3, 2, 1, 0);
TactSwitch btn;

const int pinLcdLed = 6;

void setup()
{
  app.init();

  // Initialize LCD display
  lcd.begin(16, 2);

  // Turn off the built-in LED
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  pinMode(pinLcdLed, OUTPUT);

  // Initialize input port for button
  btn.begin(7, 0, &LF_onPush, &LF_onRelease, &LF_onHold, nullptr);
}

static void printDigit(int32_t val)
{
  if (val < 0)
  {
    lcd.print("-");
    val *= -1;
  }

  if (val < 10)
    lcd.print("0");

  lcd.print(val);
}

static void printTime(int32_t sec)
{
  if (sec < 0)
    lcd.print("-");

  sec = abs(sec);
  printDigit(sec / 60);
  lcd.print(":");
  printDigit(sec % 60);
}

static bool s_blink = false;

static void display(const uint32_t tick)
{
  if (tick - app.displayTick < 1000)
    return;

  app.displayTick = tick;
  lcd.setCursor(0, 0);
  switch (app.status)
  {
  case kAppStatusInit:
    lcd.print("LAP TGT.: ");
    printTime(int32_t(lapTargets[app.lapTargetIdx] / 1000));
    break;
  default:
    printTime(int32_t(app.appTick(tick) / 1000));
    lcd.print(" ");
    lcd.print(app.lapCount * 50);
    lcd.print("m        ");
    break;
  }

  lcd.setCursor(0, 1);
  switch (app.status)
  {
  case kAppStatusInit:
    lcd.print("=READY TO START=");
    break;
  case kAppStatusRunning:
  {
    lcd.print("L");
    lcd.print(app.lapCount);
    lcd.print(" ");
    printTime(app.lapCount == 0 ? 0 : (app.laps[app.lapCount - 1] / 1000));

    lcd.print(" ");
    int32_t off = app.total - app.totalTarget;

    if (off > 0)
      lcd.print("+");

    printTime(off / 1000);
    lcd.print("     ");
    break;
  }
  case kAppStatusPaused:
    lcd.print("L");
    lcd.print(app.lapCount);
    lcd.print(s_blink ? "             " : " PAUSED      ");
    s_blink = !s_blink;
    break;
  case kAppStatusConfirmReset:
    lcd.print("L");
    lcd.print(app.lapCount);
    lcd.print(s_blink ? "             " : " RESET?      ");
    s_blink = !s_blink;
    break;
  }
}

static uint32_t s_pushTick;
static void LF_onPush(uint32_t tick)
{
  s_pushTick = tick;
}

static void LF_onRelease(uint32_t tick)
{
  tick = s_pushTick;
  switch (app.status)
  {
  case kAppStatusInit:
    app.status = kAppStatusRunning;
    app.startTick = app.lapTick = tick;
    break;
  case kAppStatusRunning:
  {
    const uint32_t idx = app.lapCount++ % lapMax;
    const uint32_t lap = tick - app.lapTick;
    app.laps[idx] = lap;
    app.total += lap;
    app.totalTarget += lapTargets[app.lapTargetIdx];
    app.lapTick = tick;
    break;
  }
  case kAppStatusPaused:
  case kAppStatusConfirmReset:
  {
    // Resume
    uint32_t pauseDur = tick - app.pauseTick;
    app.pauseTotal += pauseDur;
    app.lapTick += pauseDur;
    app.status = kAppStatusRunning;
    break;
  }
  }

  app.displayTick = 0;
  app.lcdLedTick = tick;
}

static void LF_onHold(uint32_t tick)
{
  switch (app.status)
  {
  case kAppStatusInit:
    app.lapTargetIdx = (app.lapTargetIdx + 1) % lapTargetCount;
    break;
  case kAppStatusRunning:
    app.status = kAppStatusPaused;
    app.pauseTick = tick;
    s_blink = false;
    break;
  case kAppStatusPaused:
    app.status = kAppStatusConfirmReset;
    s_blink = false;
    break;
  case kAppStatusConfirmReset:
    app.init();
    break;
  }

  app.displayTick = 0;
  app.lcdLedTick = tick;
}

static void setLcdLed(uint32_t tick)
{
  static bool lcdLed = false;

  if (!app.lcdLedTick)
    return;

  if (tick - app.lcdLedTick < 10000)
  {
    if (!lcdLed)
    {
      lcdLed = true;
      digitalWrite(pinLcdLed, HIGH);
    }
  }
  else
  {
    lcdLed = false;
    app.lcdLedTick = 0;
    digitalWrite(pinLcdLed, LOW);
  }
}

void loop()
{
  const uint32_t tick = millis();
  btn.processTick(tick);
  display(tick);
  setLcdLed(tick);
}
