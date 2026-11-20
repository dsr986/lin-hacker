#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <EEPROM.h>

// =====================================
// LCD 20x4
// =====================================

LiquidCrystal_I2C lcd(0x3F, 20, 4);

// =====================================
// KY-040
// =====================================

#define ENC_CLK 4
#define ENC_DT  5
#define ENC_SW  6

// =====================================
// POWER
// =====================================

int powerValue = 0;
const int POWER_STEP = 5;

// =====================================
// ЭКРАНЫ
// =====================================

enum Screen {
  MAIN_SCREEN,
  POWER_SCREEN
};

Screen currentScreen = MAIN_SCREEN;

// =====================================
// ЭНКОДЕР
// =====================================

int lastCLK;
int encoderPosition = 0;

// =====================================
// КНОПКА
// =====================================

bool buttonLastState = HIGH;
bool buttonPressed = false;

unsigned long buttonPressTime = 0;
const unsigned long LONG_PRESS_TIME = 1000;

// =====================================
// EEPROM
// =====================================

const uint16_t EEPROM_MAGIC = 0x4F52;
const int EEPROM_ADDRESS = 0;

struct SavedSettings {
  uint16_t magic;
  uint8_t power;
};

bool settingsSavePending = false;
unsigned long lastSettingsChange = 0;

const unsigned long EEPROM_SAVE_DELAY = 1500;

// =====================================
// СОСТОЯНИЕ LIN
// Пока только отображение интерфейса
// =====================================

const char* linStatus = "WAITING";
int generatorResponse = -1;

// =====================================
// ОБНОВЛЕНИЕ ЭКРАНА
// =====================================

bool screenChanged = true;

// =====================================
// ЗАГРУЗКА НАСТРОЕК
// =====================================

void loadSettings() {

  SavedSettings saved;
  EEPROM.get(EEPROM_ADDRESS, saved);

  if (saved.magic == EEPROM_MAGIC &&
      saved.power <= 100 &&
      saved.power % 5 == 0) {

    powerValue = saved.power;

  } else {

    powerValue = 0;
  }
}

// =====================================
// СОХРАНЕНИЕ НАСТРОЕК
// =====================================

void saveSettings() {

  SavedSettings saved;

  saved.magic = EEPROM_MAGIC;
  saved.power = powerValue;

  EEPROM.put(EEPROM_ADDRESS, saved);

  settingsSavePending = false;
}

// =====================================
// ОТЛОЖЕННОЕ СОХРАНЕНИЕ
// =====================================

void scheduleSave() {

  settingsSavePending = true;
  lastSettingsChange = millis();
}

void processEEPROM() {

  if (settingsSavePending &&
      millis() - lastSettingsChange >= EEPROM_SAVE_DELAY) {

    saveSettings();
  }
}

// =====================================
// SETUP
// =====================================

void setup() {

  pinMode(ENC_CLK, INPUT_PULLUP);
  pinMode(ENC_DT, INPUT_PULLUP);
  pinMode(ENC_SW, INPUT_PULLUP);

  lastCLK = digitalRead(ENC_CLK);

  lcd.init();
  lcd.backlight();

  loadSettings();

  showMainScreen();
}

// =====================================
// ГЛАВНЫЙ ЦИКЛ
// =====================================

void loop() {

  readEncoder();
  readButton();

  processEEPROM();

  if (screenChanged) {

    screenChanged = false;
    updateScreen();
  }
}

// =====================================
// ЧТЕНИЕ ЭНКОДЕРА
// =====================================

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

// =====================================
// ОБРАБОТКА ВРАЩЕНИЯ
// =====================================

void handleEncoder() {

  if (currentScreen == MAIN_SCREEN) {

    // В главном меню вращение меняет мощность
    changePower(encoderPosition);
    encoderPosition = 0;
  }

  else if (currentScreen == POWER_SCREEN) {

    changePower(encoderPosition);
    encoderPosition = 0;
  }

  screenChanged = true;
}

// =====================================
// ИЗМЕНЕНИЕ POWER
// =====================================

void changePower(int direction) {

  if (direction > 0) {
    powerValue += POWER_STEP;
  }

  if (direction < 0) {
    powerValue -= POWER_STEP;
  }

  powerValue = constrain(powerValue, 0, 100);

  scheduleSave();
}

// =====================================
// ЧТЕНИЕ КНОПКИ
// =====================================

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

// =====================================
// КОРОТКОЕ НАЖАТИЕ
// =====================================

void shortPress() {

  if (currentScreen == MAIN_SCREEN) {
    currentScreen = POWER_SCREEN;
  }

  else {
    currentScreen = MAIN_SCREEN;
  }

  screenChanged = true;
}

// =====================================
// ДОЛГОЕ НАЖАТИЕ
// =====================================

void goBack() {

  currentScreen = MAIN_SCREEN;
  screenChanged = true;
}

// =====================================
// ОБНОВЛЕНИЕ ЭКРАНА
// =====================================

void updateScreen() {

  if (currentScreen == MAIN_SCREEN) {
    showMainScreen();
  }

  else {
    showPowerScreen();
  }
}

// =====================================
// ГЛАВНЫЙ ЭКРАН
// =====================================

void showMainScreen() {

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("FORD LIN MASTER");

  lcd.setCursor(0, 1);
  lcd.print("POWER: ");
  printPower();
  lcd.print("%");

  lcd.setCursor(0, 2);
  lcd.print("LIN: ");
  lcd.print(linStatus);

  lcd.setCursor(0, 3);
  lcd.print("RESPONSE: ");

  if (generatorResponse < 0) {
    lcd.print("--");
  } else {
    lcd.print(generatorResponse);
    lcd.print("%");
  }
}

// =====================================
// ЭКРАН РЕГУЛИРОВКИ
// =====================================

void showPowerScreen() {

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("POWER CONTROL");

  lcd.setCursor(0, 1);
  lcd.print("TARGET: ");

  printPower();
  lcd.print("%");

  lcd.setCursor(0, 2);
  lcd.print("STEP: 5%");

  lcd.setCursor(0, 3);
  lcd.print("HOLD: BACK");
}

// =====================================
// ВЫВОД POWER
// =====================================

void printPower() {

  if (powerValue < 100) lcd.print("0");
  if (powerValue < 10) lcd.print("0");

  lcd.print(powerValue);
}