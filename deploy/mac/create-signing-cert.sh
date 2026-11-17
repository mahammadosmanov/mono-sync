#!/bin/sh
# SPDX-FileCopyrightText: (C) 2026 Mono Sync Developers
# SPDX-License-Identifier: MIT
#
# Create the long-lived self-signed code-signing certificate used for macOS releases.
# Run once, keep the output folder somewhere safe, and store the .p12 in GitHub secrets.
# Losing the certificate means every user must grant Accessibility access again.
#
# Usage: create-signing-cert.sh <output-dir>
set -eu

out="${1:?usage: create-signing-cert.sh <output-dir>}"
mkdir -p "$out"
umask 077
name="Mono Sync Code Signing"

cat > "$out/cert.cnf" <<EOF
[req]
distinguished_name = dn
x509_extensions = ext
prompt = no
[dn]
CN = $name
[ext]
basicConstraints = critical, CA:false
keyUsage = critical, digitalSignature
extendedKeyUsage = critical, codeSigning
EOF

# LibreSSL (/usr/bin/openssl) writes a PKCS#12 that `security import` accepts.
/usr/bin/openssl req -x509 -newkey rsa:3072 -nodes -days 7300 -sha256 \
  -config "$out/cert.cnf" -keyout "$out/key.pem" -out "$out/cert.pem"
password="$(/usr/bin/openssl rand -base64 24)"
/usr/bin/openssl pkcs12 -export -inkey "$out/key.pem" -in "$out/cert.pem" \
  -name "$name" -out "$out/signing.p12" -passout "pass:$password"
printf '%s' "$password" > "$out/signing.p12.password"
rm -f "$out/key.pem" "$out/cert.cnf"

echo "Created $out/signing.p12 (password in $out/signing.p12.password)."
echo "Add the GitHub secrets with:"
echo "  base64 -i \"$out/signing.p12\" | gh secret set MACOS_SIGNING_P12_BASE64"
echo "  gh secret set MACOS_SIGNING_P12_PASSWORD < \"$out/signing.p12.password\""
