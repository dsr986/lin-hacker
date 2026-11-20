#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <EEPROM.h>
#include <avr/io.h>

// ==========================
// LCD 20x4
// ==========================

LiquidCrystal_I2C lcd(0x3F, 20, 4);

// ==========================
// ЭНКОДЕР KY-040
// ==========================

#define ENC_CLK 4
#define ENC_DT  5
#define ENC_SW  6

// ==========================
// LIN TEST TRANSMITTER
// ==========================

#define LIN_TX_PIN 1

const uint8_t LIN_PID = 0x92;
const unsigned long LIN_INTERVAL = 500;

unsigned long lastLinFrame = 0;

// ==========================
// POWER
// ==========================

int powerValue = 0;
const int POWER_STEP = 5;

// ==========================
// РЕЖИМЫ РАБОТЫ
// ==========================

enum WorkMode {
  AUTO_MODE,
  BENCH_MODE
};

WorkMode workMode = AUTO_MODE;

// ==========================
// ЭКРАНЫ
// ==========================

enum Screen {
  MAIN_MENU,
  POWER_SCREEN,
  GENERATOR_SCREEN,
  MODE_SCREEN
};

Screen currentScreen = MAIN_MENU;

// ==========================
// ЭНКОДЕР
// ==========================

int lastCLK;
int encoderPosition = 0;

// ==========================
// КНОПКА
// ==========================

bool buttonLastState = HIGH;
bool buttonPressed = false;

unsigned long buttonPressTime = 0;

const unsigned long LONG_PRESS_TIME = 1000;

// ==========================
// ОБНОВЛЕНИЕ ЭКРАНА
// ==========================

bool screenChanged = true;

// ==========================
// EEPROM
// ==========================

const uint16_t EEPROM_MAGIC = 0x4F52;
const int EEPROM_ADDRESS = 0;

struct SavedSettings {
  uint16_t magic;
  uint8_t power;
  uint8_t mode;
};

bool settingsSavePending = false;
unsigned long lastSettingsChange = 0;

const unsigned long EEPROM_SAVE_DELAY = 1500;

// ==========================
// LIN CHECKSUM
// Enhanced checksum
// ==========================

uint8_t calculateLINChecksum(uint8_t pid,
                             uint8_t data0,
                             uint8_t data1,
                             uint8_t data2) {

  uint16_t sum = pid;

  sum += data0;
  sum += data1;
  sum += data2;

  while (sum > 0xFF) {
    sum = (sum & 0xFF) + (sum >> 8);
  }

  return (uint8_t)(~sum);
}

// ==========================
// LIN BREAK
// ==========================

void sendLINBreak() {

  Serial.flush();

  UCSR0B &= ~_BV(TXEN0);

  pinMode(LIN_TX_PIN, OUTPUT);

  digitalWrite(LIN_TX_PIN, LOW);
  delayMicroseconds(1500);

  digitalWrite(LIN_TX_PIN, HIGH);
  delayMicroseconds(200);

  UCSR0B |= _BV(TXEN0);
}

// ==========================
// ОТПРАВКА LIN КАДРА
// ==========================

void sendLINFrame() {

  // POWER передаём числом от 0 до 100
  uint8_t data0 = (uint8_t)powerValue;

  // Пока тестовые байты
  uint8_t data1 = 0x02;
  uint8_t data2 = 0x03;

  uint8_t checksum = calculateLINChecksum(
    LIN_PID,
    data0,
    data1,
    data2
  );

  sendLINBreak();

  Serial.write(0x55);       // SYNC
  Serial.write(LIN_PID);    // PID
  Serial.write(data0);      // POWER
  Serial.write(data1);      // TEST DATA
  Serial.write(data2);      // TEST DATA
  Serial.write(checksum);   // CHECKSUM

  Serial.flush();
}

// ==========================
// ОБРАБОТКА LIN
// ==========================

void processLIN() {

  // В AUTO передача отключена
  if (workMode != BENCH_MODE) {
    return;
  }

  if (millis() - lastLinFrame >= LIN_INTERVAL) {

    lastLinFrame = millis();

    sendLINFrame();
  }
}

// ==========================
// ЗАГРУЗКА НАСТРОЕК
// ==========================

void loadSettings() {

  SavedSettings saved;

  EEPROM.get(EEPROM_ADDRESS, saved);

  if (saved.magic == EEPROM_MAGIC &&
      saved.power <= 100 &&
      saved.power % 5 == 0 &&
      saved.mode <= 1) {

    powerValue = saved.power;
    workMode = (WorkMode)saved.mode;

  } else {

    powerValue = 0;
    workMode = AUTO_MODE;
  }
}

// ==========================
// СОХРАНЕНИЕ НАСТРОЕК
// ==========================

void saveSettings() {

  SavedSettings saved;

  saved.magic = EEPROM_MAGIC;
  saved.power = powerValue;
  saved.mode = (uint8_t)workMode;

  EEPROM.put(EEPROM_ADDRESS, saved);

  settingsSavePending = false;
}

// ==========================
// ЗАПЛАНИРОВАТЬ СОХРАНЕНИЕ
// ==========================

void scheduleSave() {

  settingsSavePending = true;
  lastSettingsChange = millis();
}

// ==========================
// ПРОВЕРКА СОХРАНЕНИЯ
// ==========================

void processEEPROM() {

  if (settingsSavePending &&
      millis() - lastSettingsChange >= EEPROM_SAVE_DELAY) {

    saveSettings();
  }
}

// ==========================
// SETUP
// ==========================

void setup() {

  pinMode(ENC_CLK, INPUT_PULLUP);
  pinMode(ENC_DT, INPUT_PULLUP);
  pinMode(ENC_SW, INPUT_PULLUP);

  lastCLK = digitalRead(ENC_CLK);

  lcd.init();
  lcd.backlight();

  loadSettings();

  // UART для тестовой LIN-передачи на D1
  Serial.begin(9600);

  pinMode(LIN_TX_PIN, OUTPUT);
  digitalWrite(LIN_TX_PIN, HIGH);

  lastLinFrame = millis();

  showMainMenu();
}

// ==========================
// ГЛАВНЫЙ ЦИКЛ
// ==========================

void loop() {

  readEncoder();
  readButton();

  processEEPROM();
  processLIN();

  if (screenChanged) {

    screenChanged = false;
    updateScreen();
  }
}

// ==========================
// ЧТЕНИЕ ЭНКОДЕРА
// ==========================

void readEncoder() {

  int currentCLK = digitalRead(ENC_CLK);

  if (currentCLK != lastCLK && currentCLK == LOW) {

    if (digitalRead(ENC_DT) != currentCLK) {
      encoderPosition++;
    } else {
      encoderPosition--;
    }

    handleEncoder();
  }

  lastCLK = currentCLK;
}

// ==========================
// ОБРАБОТКА ВРАЩЕНИЯ
// ==========================

void handleEncoder() {

  // ГЛАВНОЕ МЕНЮ
  if (currentScreen == MAIN_MENU) {

    encoderPosition = constrain(encoderPosition, 0, 2);

    screenChanged = true;
  }

  // POWER
  else if (currentScreen == POWER_SCREEN) {

    int oldPower = powerValue;

    if (encoderPosition > 0) {
      powerValue += POWER_STEP;
    }

    if (encoderPosition < 0) {
      powerValue -= POWER_STEP;
    }

    powerValue = constrain(powerValue, 0, 100);

    encoderPosition = 0;

    if (powerValue != oldPower) {
      scheduleSave();
    }

    screenChanged = true;
  }

  // MODE
  else if (currentScreen == MODE_SCREEN) {

    WorkMode oldMode = workMode;

    if (encoderPosition > 0) {
      workMode = BENCH_MODE;
    }

    if (encoderPosition < 0) {
      workMode = AUTO_MODE;
    }

    encoderPosition = 0;

    if (workMode != oldMode) {
      scheduleSave();
      lastLinFrame = millis();
    }

    screenChanged = true;
  }
}

// ==========================
// ЧТЕНИЕ КНОПКИ
// ==========================

void readButton() {

  bool buttonState = digitalRead(ENC_SW);

  if (buttonLastState == HIGH && buttonState == LOW) {

    buttonPressed = true;
    buttonPressTime = millis();
  }

  if (buttonLastState == LOW && buttonState == HIGH) {

    if (buttonPressed) {

      unsigned long pressDuration =
        millis() - buttonPressTime;

      if (pressDuration >= LONG_PRESS_TIME) {
        goBack();
      } else {
        shortPress();
      }

      buttonPressed = false;
    }
  }

  buttonLastState = buttonState;
}

// ==========================
// КОРОТКОЕ НАЖАТИЕ
// ==========================

void shortPress() {

  if (currentScreen == MAIN_MENU) {

    if (encoderPosition == 0) {
      currentScreen = POWER_SCREEN;
    }

    else if (encoderPosition == 1) {
      currentScreen = GENERATOR_SCREEN;
    }

    else if (encoderPosition == 2) {
      currentScreen = MODE_SCREEN;
    }
  }

  else if (currentScreen == GENERATOR_SCREEN) {

    currentScreen = MAIN_MENU;
  }

  screenChanged = true;
}

// ==========================
// ДОЛГОЕ НАЖАТИЕ
// ==========================

void goBack() {

  if (currentScreen != MAIN_MENU) {
    currentScreen = MAIN_MENU;
  }

  screenChanged = true;
}

// ==========================
// ОБНОВЛЕНИЕ ЭКРАНА
// ==========================

void updateScreen() {

  switch (currentScreen) {

    case MAIN_MENU:
      showMainMenu();
      break;

    case POWER_SCREEN:
      showPower();
      break;

    case GENERATOR_SCREEN:
      showGenerator();
      break;

    case MODE_SCREEN:
      showMode();
      break;
  }
}

// ==========================
// ГЛАВНОЕ МЕНЮ
// ==========================

void showMainMenu() {

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("FORD GEN CONTROL");

  lcd.setCursor(0, 1);
  lcd.print(encoderPosition == 0 ? "> " : "  ");
  lcd.print("POWER: ");
  printPower();
  lcd.print("%");

  lcd.setCursor(0, 2);
  lcd.print(encoderPosition == 1 ? "> " : "  ");
  lcd.print("GENERATOR: FORD");

  lcd.setCursor(0, 3);
  lcd.print(encoderPosition == 2 ? "> " : "  ");
  lcd.print("MODE: ");
  printMode();
}

// ==========================
// ЭКРАН POWER
// ==========================

void showPower() {

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("POWER CONTROL");

  lcd.setCursor(0, 1);
  lcd.print("TARGET: ");

  printPower();
  lcd.print("%");

  lcd.setCursor(0, 3);
  lcd.print("ROTATE: CHANGE");
}

// ==========================
// ЭКРАН GENERATOR
// ==========================

void showGenerator() {

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("GENERATOR PROFILE");

  lcd.setCursor(0, 1);
  lcd.print("> FORD");

  lcd.setCursor(0, 3);
  lcd.print("HOLD: BACK");
}

// ==========================
// ЭКРАН MODE
// ==========================

void showMode() {

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("WORK MODE");

  lcd.setCursor(0, 1);

  if (workMode == AUTO_MODE) {
    lcd.print("> AUTO");
  } else {
    lcd.print("> BENCH");
  }

  lcd.setCursor(0, 3);
  lcd.print("ROTATE: CHANGE");
}

// ==========================
// ВЫВОД POWER
// ==========================

void printPower() {

  if (powerValue < 100) lcd.print("0");
  if (powerValue < 10) lcd.print("0");

  lcd.print(powerValue);
}

// ==========================
// ВЫВОД РЕЖИМА
// ==========================

void printMode() {

  if (workMode == AUTO_MODE) {
    lcd.print("AUTO");
  } else {
    lcd.print("BENCH");
  }
}
