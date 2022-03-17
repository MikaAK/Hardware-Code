class Probe {
  private:
    bool _state;
    uint8_t _pin;

  public:
    Probe(uint8_t pin) : _pin(pin) {}

    void setup() {
      pinMode(_pin, INPUT_PULLUP);
    }

    bool isDisconnected() {
      bool v = digitalRead(_pin);

      if (v != _state) {
        _state = v;

        if (_state == HIGH) {
          Serial.println("PROBE_DISCONNECTED");

          return true;
        } else {
          Serial.println("PROBE_CONNECTED");
        }
      }

      return false;
    }
};

Probe aligatorProbe(6);

void setup() {
  Serial.begin(115200);
  aligatorProbe.setup();

  pinMode(LED_BUILTIN, OUTPUT);
  analogWrite(LED_BUILTIN, 0);
}

void loop() {
  // put your main code here, to run repeatedly:
  if (aligatorProbe.isDisconnected()) {
    digitalWrite(LED_BUILTIN, LOW);
  } else {
    digitalWrite(LED_BUILTIN, HIGH);
  }

  delay(1);
}
