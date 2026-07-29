// Minimal MCP4728 FastWrite driver (Mainboard mcpUpdate channel map).
// Addresses 0x63 / 0x64 / 0x65 — monosynth remap still TBD (PINOUT open item).

#ifdef ENABLE_MCP4728

#include <Wire.h>

static constexpr uint8_t MCP4728_ADDR_1 = 0x63;
static constexpr uint8_t MCP4728_ADDR_2 = 0x64;
static constexpr uint8_t MCP4728_ADDR_3 = 0x65;

// Fast Write: 8 bytes, channels A→D, PD=00, 12-bit value.
static void mcp4728_fastWrite(uint8_t addr, uint16_t a, uint16_t b, uint16_t c, uint16_t d) {
  Wire.beginTransmission(addr);
  auto put = [](uint16_t v) {
    v &= 0x0FFF;
    Wire.write((uint8_t)((v >> 8) & 0x0F));
    Wire.write((uint8_t)(v & 0xFF));
  };
  put(a);
  put(b);
  put(c);
  put(d);
  Wire.endTransmission();
}

void init_MCP4728() {
  Wire.setSDA(MCP4728_SDA_PIN);
  Wire.setSCL(MCP4728_SCL_PIN);
  Wire.setClock(1000000);
  Wire.begin();

  // Idle high (same as Mainboard boot).
  mcp4728_fastWrite(MCP4728_ADDR_1, 4095, 4095, 4095, 4095);
  mcp4728_fastWrite(MCP4728_ADDR_2, 4095, 4095, 4095, 4095);
  mcp4728_fastWrite(MCP4728_ADDR_3, 4095, 4095, 4095, 4095);
}

// Push SQR1/SQR2/Sub levels — Mainboard channel comments preserved.
void mcpUpdate() {
  // V1 OSC1, V2 OSC2, V2 OSC1, V3 OSC2
  mcp4728_fastWrite(MCP4728_ADDR_1, SQR1Level, SQR2Level, SQR1Level, SQR2Level);
  // V3 OSC1, V4 OSC2, V4 OSC1, SUB3
  mcp4728_fastWrite(MCP4728_ADDR_2, SQR1Level, SQR2Level, SQR1Level, SubLevel);
  // SUB4, SUB1, SUB2, V1 OSC2
  mcp4728_fastWrite(MCP4728_ADDR_3, SubLevel, SubLevel, SubLevel, SQR2Level);
}

#else  // !ENABLE_MCP4728

void init_MCP4728() {}
void mcpUpdate() {}

#endif
