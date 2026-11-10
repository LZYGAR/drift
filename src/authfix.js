// Apple's sign-in page waits on three things this Qt browser does not do:
// the passkey capability check never settles, blob workers never start,
// and a worker reply has to land on a later turn or Apple drops it.
// The password step needs both a SHA-1 stamp and an SRP reply.
(function () {
  if (window.__clickAuthFix) return;
  window.__clickAuthFix = true;

  var credential = window.PublicKeyCredential;
  if (credential) {
    var capabilities = {
      conditionalCreate: false,
      conditionalGet: false,
      hybridTransport: false,
      passkeyPlatformAuthenticator: false,
      userVerifyingPlatformAuthenticator: false,
      relatedOrigins: false,
      signalAllAcceptedCredentials: false,
      signalCurrentUserDetails: false,
      signalUnknownCredential: false
    };
    credential.getClientCapabilities = function () { return Promise.resolve(capabilities); };
    credential.isConditionalMediationAvailable = function () { return Promise.resolve(false); };
    credential.isUserVerifyingPlatformAuthenticatorAvailable = function () { return Promise.resolve(false); };
  }

  if (!window.Worker) return;
  var NativeWorker = window.Worker;

  // RFC 5054 2048-bit group. g is 2.
  var N_HEX = "AC6BDB41324A9A9BF166DE5E1389582FAF72B6651987EE07FC3192943DB56050A37329CBB4A099ED8193E0757767A13DD52312AB4B03310DCD7F48A9DA04FD50E8083969EDB767B0CF6095179A163AB3661A05FBD5FAAAE82918A9962F0B93B855F97993EC975EEAA80D740ADBF4FF747359D041D5C33EA71D281E446B14773BCA97B43A23FB801676BD207A436C6481F1D2B9078717461A5B9D32E688F87748544523B524B0D57D5EA77A2775D2ECFA032CFBDBF52FB3786160279004E57AE6AF874E7303CE53299CCC041C7BC308D82A5698F3A8D0C38271AE35F8E9DBFBB694B5C803D89F7AE435DE236D525F54759B65E372FCD68EF20FA7111F9E4AFF73";
  var N_BYTES = hexToBytes(N_HEX);
  var N_BI = bytesToBigInt(N_BYTES);
  var G_BI = 2n;

  function rotl(n, s) { return (n << s) | (n >>> (32 - s)); }

  function sha1(message) {
    var bytes = [];
    for (var i = 0; i < message.length; i++) bytes.push(message.charCodeAt(i) & 255);
    var bitLen = bytes.length * 8;
    bytes.push(128);
    while (bytes.length % 64 !== 56) bytes.push(0);
    bytes.push(0, 0, 0, 0, (bitLen >>> 24) & 255, (bitLen >>> 16) & 255, (bitLen >>> 8) & 255, bitLen & 255);
    var h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE, h3 = 0x10325476, h4 = 0xC3D2E1F0;
    var w = new Array(80);
    for (var off = 0; off < bytes.length; off += 64) {
      for (var j = 0; j < 16; j++) {
        w[j] = (bytes[off + j * 4] << 24) | (bytes[off + j * 4 + 1] << 16) | (bytes[off + j * 4 + 2] << 8) | bytes[off + j * 4 + 3];
      }
      for (var k = 16; k < 80; k++) w[k] = rotl(w[k - 3] ^ w[k - 8] ^ w[k - 14] ^ w[k - 16], 1);
      var a = h0, b = h1, c = h2, d = h3, e = h4;
      for (var t = 0; t < 80; t++) {
        var f, kk;
        if (t < 20) { f = (b & c) | ((~b) & d); kk = 0x5A827999; }
        else if (t < 40) { f = b ^ c ^ d; kk = 0x6ED9EBA1; }
        else if (t < 60) { f = (b & c) | (b & d) | (c & d); kk = 0x8F1BBCDC; }
        else { f = b ^ c ^ d; kk = 0xCA62C1D6; }
        var temp = (rotl(a, 5) + f + e + kk + w[t]) | 0;
        e = d; d = c; c = rotl(b, 30); b = a; a = temp;
      }
      h0 = (h0 + a) | 0; h1 = (h1 + b) | 0; h2 = (h2 + c) | 0; h3 = (h3 + d) | 0; h4 = (h4 + e) | 0;
    }
    return [h0, h1, h2, h3, h4];
  }

  function leadingZeros(words, bits) {
    var count = 0;
    for (var i = 0; i < words.length && count < bits; i++) {
      for (var bit = 31; bit >= 0; bit--) {
        if ((words[i] >>> bit) & 1) return count;
        count++;
        if (count >= bits) return count;
      }
    }
    return count;
  }

  function sha1Hex(message) {
    var words = sha1(message);
    var hex = "";
    for (var i = 0; i < words.length; i++) hex += (words[i] >>> 0).toString(16).padStart(8, "0");
    return hex;
  }

  function hexToBytes(hex) {
    var out = new Uint8Array(hex.length / 2);
    for (var i = 0; i < out.length; i++) out[i] = parseInt(hex.substr(i * 2, 2), 16);
    return out;
  }

  function bytesToBigInt(bytes) {
    var hex = "";
    for (var i = 0; i < bytes.length; i++) hex += bytes[i].toString(16).padStart(2, "0");
    if (!hex) return 0n;
    return BigInt("0x" + hex);
  }

  function bigIntToBytes(value) {
    if (value < 0n) value = 0n;
    var hex = value.toString(16);
    if (hex.length % 2) hex = "0" + hex;
    return hexToBytes(hex);
  }

  function padBytes(src, length) {
    var out = new Uint8Array(length);
    if (src.length >= length) {
      out.set(src.subarray(src.length - length));
      return out;
    }
    out.set(src, length - src.length);
    return out;
  }

  function concatBytes(parts) {
    var length = 0;
    for (var i = 0; i < parts.length; i++) length += parts[i].length;
    var out = new Uint8Array(length);
    var offset = 0;
    for (var j = 0; j < parts.length; j++) {
      out.set(parts[j], offset);
      offset += parts[j].length;
    }
    return out;
  }

  function bytesToBase64(bytes) {
    var text = "";
    for (var i = 0; i < bytes.length; i++) text += String.fromCharCode(bytes[i]);
    return btoa(text);
  }

  function base64ToBytes(value) {
    var clean = String(value || "").replace(/\s/g, "").replace(/-/g, "+").replace(/_/g, "/");
    while (clean.length % 4) clean += "=";
    var text = atob(clean);
    var out = new Uint8Array(text.length);
    for (var i = 0; i < text.length; i++) out[i] = text.charCodeAt(i);
    return out;
  }

  function mod(value, modulus) {
    var result = value % modulus;
    if (result < 0n) result += modulus;
    return result;
  }

  function modPow(base, exp, modulus) {
    if (modulus === 1n) return 0n;
    var result = 1n;
    base = mod(base, modulus);
    while (exp > 0n) {
      if (exp & 1n) result = (result * base) % modulus;
      exp >>= 1n;
      base = (base * base) % modulus;
    }
    return result;
  }

  function sha256(bytes) {
    return crypto.subtle.digest("SHA-256", bytes).then(function (digest) {
      return new Uint8Array(digest);
    });
  }

  function utf8(text) {
    return new TextEncoder().encode(String(text));
  }

  function passwordBytes(password) {
    if (password instanceof Uint8Array) return password;
    if (password instanceof ArrayBuffer) return new Uint8Array(password);
    return utf8(password == null ? "" : String(password));
  }

  function deriveKey(password, salt, iterations, protocol) {
    var encoded = passwordBytes(password);
    return sha256(encoded).then(function (hashed) {
      if (!iterations) return hashed;
      var material = protocol === "s2k" ? hashed : utf8(bytesToHex(hashed));
      return crypto.subtle.importKey("raw", material, "PBKDF2", false, ["deriveBits"]).then(function (key) {
        return crypto.subtle.deriveBits({
          name: "PBKDF2",
          salt: salt,
          iterations: iterations,
          hash: "SHA-256"
        }, key, 256);
      }).then(function (bits) { return new Uint8Array(bits); });
    });
  }

  function bytesToHex(bytes) {
    var hex = "";
    for (var i = 0; i < bytes.length; i++) hex += bytes[i].toString(16).padStart(2, "0");
    return hex;
  }

  function userHash(derived) {
    var inner = concatBytes([new Uint8Array([0x3a]), derived]);
    return sha256(inner);
  }

  function multiplier() {
    return sha256(concatBytes([N_BYTES, padBytes(new Uint8Array([2]), N_BYTES.length)]));
  }

  function evidence(secret, accountName, values) {
    var iterations = Number(values.iterations) || 0;
    var protocol = values.protocol || "s2k";
    var salt = values.salt instanceof Uint8Array ? values.salt : base64ToBytes(values.salt);
    var server = values.serverPublicValue instanceof Uint8Array ? values.serverPublicValue : base64ToBytes(values.serverPublicValue);
    var secretBi = bytesToBigInt(secret);
    var publicBi = modPow(G_BI, secretBi, N_BI);
    var A = padBytes(bigIntToBytes(publicBi), N_BYTES.length);
    var identity = utf8(String(accountName || "").toLowerCase());
    return deriveKey(values.password, salt, iterations, protocol).then(function (derived) {
      return userHash(derived).then(function (inner) {
        return sha256(concatBytes([salt, inner]));
      }).then(function (xBytes) {
        return multiplier().then(function (kBytes) {
          var x = bytesToBigInt(xBytes);
          var k = bytesToBigInt(kBytes);
          var B = bytesToBigInt(server);
          var uBytes = sha256(concatBytes([padBytes(A, N_BYTES.length), padBytes(server, N_BYTES.length)]));
          return uBytes.then(function (uHash) {
            var u = bytesToBigInt(uHash);
            var gx = modPow(G_BI, x, N_BI);
            var base = mod(B - mod(k * gx, N_BI), N_BI);
            var exp = secretBi + u * x;
            var S = modPow(base, exp, N_BI);
            return sha256(padBytes(bigIntToBytes(S), N_BYTES.length)).then(function (K) {
              return sha256(N_BYTES).then(function (hn) {
                return sha256(padBytes(new Uint8Array([2]), N_BYTES.length)).then(function (hg) {
                  var mixed = bigIntToBytes(bytesToBigInt(hn) ^ bytesToBigInt(hg));
                  return sha256(identity).then(function (hi) {
                    return sha256(concatBytes([mixed, hi, salt, A, server, K])).then(function (M1) {
                      return sha256(concatBytes([A, M1, K])).then(function (M2) {
                        return {
                          a: bytesToBase64(A),
                          m1: bytesToBase64(M1),
                          m2: bytesToBase64(M2),
                          k: bytesToBase64(K)
                        };
                      });
                    });
                  });
                });
              });
            });
          });
        });
      });
    });
  }

  function publicA(secret) {
    return padBytes(bigIntToBytes(modPow(G_BI, bytesToBigInt(secret), N_BI)), N_BYTES.length);
  }

  function mint(challenge, bits, options) {
    var separator = options && typeof options.separator === "string" && options.separator ? options.separator : ":";
    var checkBits = !options || options.checkZeroBitsSequence !== false;
    var stamp = new Date().toISOString().slice(0, 19).replace(/[-:T]/g, "");
    var prefix = "1:" + bits + ":" + stamp + ":" + challenge;
    var limit = bits > 22 ? 200000 : 4000000;
    var counter = 0;
    function step() {
      var end = Math.min(limit, counter + 4000);
      for (; counter < end; counter++) {
        var token = prefix + separator + counter;
        if (checkBits) {
          if (leadingZeros(sha1(token), bits) >= bits) return token;
        } else if (sha1Hex(token).indexOf(new Array(bits + 1).join("0")) === 0) {
          return token;
        }
      }
      if (counter >= limit) return prefix + separator + "0";
      return new Promise(function (resolve) {
        setTimeout(function () { resolve(step()); }, 0);
      });
    }
    if (!bits || bits < 0) return prefix + separator + "0";
    return step();
  }

  function deliver(api, data) {
    if (api.dead) return;
    var event = { data: data };
    if (typeof api.onmessage === "function") api.onmessage(event);
    for (var i = 0; i < api.listeners.length; i++) {
      if (api.listeners[i] !== api.onmessage) api.listeners[i](event);
    }
  }

  function handleHashcash(data) {
    var payload = data;
    var challenge = payload && payload.length ? payload[0] : "";
    var bits = payload && payload.length > 1 ? parseInt(payload[1], 10) : 0;
    var options = payload && payload.length > 2 ? payload[2] : null;
    if (!bits || bits < 0) bits = 0;
    var started = Date.now();
    return Promise.resolve(mint(String(challenge), bits, options)).then(function (stamp) {
      return JSON.stringify({ hashcash: stamp, performance: Date.now() - started });
    });
  }

  function handleSrp(api, message) {
    var name = message.name;
    var values = message.values || {};
    if (name === "getInitData") {
      var accountName = values.accountName == null ? "" : String(values.accountName);
      var secret = crypto.getRandomValues(new Uint8Array(256));
      api.srp = { accountName: accountName, secret: secret };
      return Promise.resolve({
        name: "initData",
        values: { a: bytesToBase64(publicA(secret)) },
        accountName: accountName,
        initUrl: values.initUrl
      });
    }
    if ((name === "getCompleteData" || name === "getCompleteERP") && api.srp) {
      return evidence(api.srp.secret, api.srp.accountName, values).then(function (proof) {
        var reply = { m1: proof.m1, m2: proof.m2, c: values.c };
        if (name === "getCompleteERP") reply.k = proof.k;
        return { name: "completeData", values: reply };
      });
    }
    if (name === "getEnrollmentData") {
      var salt = values.srpSalt instanceof Uint8Array ? values.srpSalt : base64ToBytes(values.srpSalt);
      var rounds = Number(values.iterations) || 0;
      return deriveKey(values.password, salt, rounds, values.protocol || "s2k").then(function (derived) {
        return userHash(derived).then(function (inner) {
          return sha256(concatBytes([salt, inner]));
        }).then(function (xBytes) {
          var verifier = bigIntToBytes(modPow(G_BI, bytesToBigInt(xBytes), N_BI));
          return { name: "enrollmentData", values: { verifier: bytesToBase64(verifier) } };
        });
      });
    }
    return Promise.resolve(undefined);
  }

  function fakeWorker() {
    var api = {
      onmessage: null,
      onerror: null,
      listeners: [],
      dead: false,
      tail: Promise.resolve(),
      srp: null,
      addEventListener: function (type, fn) {
        if (type === "message" && typeof fn === "function") api.listeners.push(fn);
        if (type === "error" && typeof fn === "function") api.onerror = fn;
      },
      removeEventListener: function (type, fn) {
        if (type !== "message") return;
        api.listeners = api.listeners.filter(function (listener) { return listener !== fn; });
      },
      terminate: function () { api.dead = true; },
      postMessage: function (data) {
        api.tail = api.tail.then(function () {
          return new Promise(function (resolve) {
            setTimeout(function () {
              var payload = data;
              if (typeof payload === "string") {
                try { payload = JSON.parse(payload); } catch (error) { payload = data; }
              }
              var job = payload && typeof payload.name === "string" ? handleSrp(api, payload) : handleHashcash(payload);
              Promise.resolve(job).then(function (reply) {
                if (reply !== undefined) deliver(api, reply);
                resolve();
              }, function () {
                if (typeof api.onerror === "function") api.onerror({ message: "worker" });
                resolve();
              });
            }, 0);
          });
        });
      }
    };
    return api;
  }

  window.Worker = function (url) {
    var href = "";
    try { href = String(url); } catch (error) { href = ""; }
    if (href.indexOf("blob:") === 0) return fakeWorker();
    return new NativeWorker(url);
  };
})();
