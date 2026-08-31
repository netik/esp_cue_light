#!/usr/bin/env bash
set -euo pipefail

LIB_DIR="${HOME}/Documents/Arduino/libraries"
AEFS_DIR="${LIB_DIR}/AsyncEspFsWebserver"
AWS_CPP="${AEFS_DIR}/src/AsyncFsWebServer.cpp"
CM_CPP="${AEFS_DIR}/src/CredentialManager.cpp"

if [[ ! -f "${AWS_CPP}" || ! -f "${CM_CPP}" ]]; then
  echo "AsyncEspFsWebserver not installed; skipping patches."
  exit 0
fi

python3 - "${AWS_CPP}" "${CM_CPP}" <<'PY'
import sys
from pathlib import Path

aws_path = Path(sys.argv[1])
cm_path = Path(sys.argv[2])

aws = aws_path.read_text()
cm = cm_path.read_text()
aws_changed = False
cm_changed = False

upsert_old = """            if (!m_credentialManager->updateCredential(params.config, params.password.c_str())) {
                m_credentialManager->addCredential(params.config, params.password.c_str());
            }"""

upsert_new = """            if (m_credentialManager->checkSSIDExists(params.config.ssid)) {
                m_credentialManager->updateCredential(params.config, params.password.c_str());
            } else {
                m_credentialManager->addCredential(params.config, params.password.c_str());
            }"""

if upsert_new in aws:
    print("Credential upsert patch already applied.")
elif upsert_old in aws:
    aws = aws.replace(upsert_old, upsert_new)
    aws_changed = True
    print("Applied credential upsert patch.")
else:
    print("WARNING: Could not find credential upsert code in AsyncFsWebServer.cpp; library may have changed.")

encrypt_old = """  // Apply PKCS7 padding
  uint8_t *padded = new uint8_t[64];
  uint16_t padded_len = 0;
  applyPKCS7Padding((uint8_t *)plaintext, plaintext_len, padded, padded_len);"""

encrypt_new = """  // Apply PKCS7 padding (stack buffer avoids heap abort on ESP8266 after WiFi connect)
  uint8_t padded[64];
  uint16_t padded_len = 0;
  applyPKCS7Padding((uint8_t *)plaintext, plaintext_len, padded, padded_len);"""

if encrypt_new in cm:
    print("Encrypt stack-buffer patch already applied.")
elif encrypt_old in cm:
    cm = cm.replace(encrypt_old, encrypt_new)
    cm = cm.replace("  delete[] padded;\n", "")
    cm_changed = True
    print("Applied encrypt stack-buffer patch.")
else:
    print("WARNING: Could not find encryptPassword heap allocation in CredentialManager.cpp; library may have changed.")

decrypt_old = """  uint8_t iv[AES_BLOCK_SIZE] = {0};
  uint8_t *decrypted = new uint8_t[cipher_len];"""

decrypt_new = """  uint8_t iv[AES_BLOCK_SIZE] = {0};
  uint8_t decrypted[64];
  if (cipher_len > sizeof(decrypted)) {
    log_error("Ciphertext too long");
    return false;
  }"""

if decrypt_new in cm:
    print("Decrypt stack-buffer patch already applied.")
elif decrypt_old in cm:
    cm = cm.replace(decrypt_old, decrypt_new)
    cm = cm.replace("  delete[] decrypted;\n", "")
    cm_changed = True
    print("Applied decrypt stack-buffer patch.")
else:
    print("WARNING: Could not find decryptPassword heap allocation in CredentialManager.cpp; library may have changed.")

if aws_changed:
    aws_path.write_text(aws)
if cm_changed:
    cm_path.write_text(cm)
PY

echo "AsyncEspFsWebserver patches complete."
