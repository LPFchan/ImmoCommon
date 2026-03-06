#pragma once

// Pre-shared 128-bit key for AES-128-CCM MIC. Set your own 16 bytes; must match the fob when you use one.
// Provision via Whimbrel or create `guillemot_secrets_local.h` (gitignored) and define GUILLEMOT_PSK_BYTES. See PROTOCOL.md.
//
// Example (replace with your key):
//   #define GUILLEMOT_PSK_BYTES 0x01,0x02,... (16 bytes total)
//
#define GUILLEMOT_PSK_BYTES  \
  0x00, 0x00, 0x00, 0x00,    \
  0x00, 0x00, 0x00, 0x00,    \
  0x00, 0x00, 0x00, 0x00,    \
  0x00, 0x00, 0x00, 0x00

