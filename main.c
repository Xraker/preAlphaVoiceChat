/*
////////////////////////////////////////////////////////////////////////////////
// ANTIGRAVITY MUST OBEY RULES
////////////////////////////////////////////////////////////////////////////////
// 1. Any deletion of a file must be asked and confirmed with the user before proceeding.
// 2. Any download of files, packages, or tools must be asked and confirmed with the user before proceeding.
// 3. Git must not be used unless the user specifically asks for it.
// 4. Agents must not be used unless the user specifically asks for it.
// 5. Speak English in general.
// 6. MUST use the .bat file (build.bat) to build the project at all times.
// 7. Single C source file architecture: keep code grouped with //////// borders.
// 8. Do not start implementation coding until explicitly instructed by the user.

////////////////////////////////////////////////////////////////////////////////
// TO-DO LIST (DEFERRED - NOT BEING IMPLEMENTED THIS SESSION)
////////////////////////////////////////////////////////////////////////////////
// [ ] Audio Compression (Opus / Codecs):
//     - Defer integrating Opus or other compression libraries.
//     - Start with raw 16 kHz uncompressed PCM (~32 KB/s) to verify baseline audio.
//     - Measure latency and bandwidth before deciding on compression.
// [ ] Push-to-Talk (PTT) & Shortcuts:
//     - Global keyboard hooks / GetAsyncKeyState bindings for hotkeys.
//     - Hold-to-speak logic.
// [ ] UI Volume & Gain Sliders:
//     - Input gain adjustment slider.
//     - Output playback volume slider.
//     - Dedicated Mute microphone button.
// [ ] Acoustic Echo Cancellation (AEC) & Noise Suppression:
//     - Users are expected to wear headphones for early versions.
//     - Advanced DSP echo suppression deferred.
// [ ] Complex NAT Traversal (STUN / TURN / ICE):
//     - Solved by using the lightweight Relay Server architecture.
// [ ] Dynamic Adaptive Jitter Buffer:
//     - Starting with the simplest fixed ring buffer.
//     - Advanced adaptive time-stretching / drift compensation deferred.
// [ ] Multi-Room Hierarchy & User Management:
//     - User avatars, member list panes, permission systems.

////////////////////////////////////////////////////////////////////////////////
// PROJECT DOCUMENTATION & DESIGN DECISIONS
////////////////////////////////////////////////////////////////////////////////
// Project Name:
//   preAlphaVoiceChat
//
// Description:
//   A lightweight, low-latency voice chat program written purely in C targeting
//   the native Windows 32-bit/64-bit API. Operates over the public internet
//   using a lightweight UDP relay server and native Win32 client.
//
// Core Design Decisions:
//   1. Language & Environment:
//      - Pure C (C99/C11) compiled with Clang targeting Windows MSVC ABI.
//      - Single unified source file (main.c) with visual section dividers.
//      - Built exclusively via build.bat with zero external package downloads.
//
//   2. Audio Engine (WASAPI):
//      - Low-latency Windows Audio Session API (WASAPI) in event-driven mode
//        (AUDCLNT_STREAMFLAGS_EVENTCALLBACK).
//      - Interface accessed in pure C via COM vtables (lpVtbl).
//      - Real-time priority assigned via AvSetMmThreadCharacteristics ("Pro Audio").
//      - Format: 16,000 Hz, 16-bit Mono raw PCM (Wideband voice, crystal clear,
//        ~32 KB/s bandwidth).
//      - Device handling: Enumerates audio endpoints, defaults to system default.
//
//   3. Networking & Topology:
//      - Lightweight Central UDP Relay Server + Client in one binary.
//      - Overcomes NAT traversal / firewall hurdles on the public internet without
//        requiring complex STUN/TURN implementations.
//      - Transport: Winsock2 UDP (SOCK_DGRAM) for immediate, non-blocking delivery.
//
//   4. Buffer & Latency Strategy:
//      - Simplest ring buffer with sequence number tracking to detect dropped
//        or out-of-order packets.
//      - Configured for ultra-low latency floor (~3ms to 15ms target when conditions
//        are optimal).
//
//   5. Security & Encryption:
//      - Windows native Cryptography Next Generation (CNG / bcrypt.dll).
//      - AES encryption with a Pre-Shared Key (PSK). Zero external crypto libraries.
//      - Microsecond execution time via CPU hardware AES-NI instructions.
//
//   6. Telemetry & Measurement Engine:
//      - Live toggleable performance metrics:
//        * Outgoing & Incoming bandwidth (KB/s).
//        * Round-Trip Time (RTT) / Latency (ms).
//        * Packet Loss % and out-of-order count.
//        * RFC 3550 Jitter calculation (ms).
//        * Ring buffer depth / watermark.
//        * Processing time overhead (microseconds per audio frame).
//
//   7. User Interface:
//      - Minimal native Win32 GUI window (CreateWindowEx, standard controls).
//      - Connection input fields (Relay IP, Port, Room/Key).
//      - Connect / Disconnect and Telemetry Toggle buttons.
//
// Must Do:
//   - Keep code strictly in pure C within main.c, organized with /////// dividers.
//   - Always compile and link using build.bat.
//   - Keep external dependencies at 0 (only link standard Windows DLLs).
//   - Always ask before downloading any files/packages or deleting any files.
//
// Must Not Do:
//   - Do not use Git commands unless explicitly requested.
//   - Do not invoke automated agents unless explicitly requested.
//   - Do not download third-party libraries (FFmpeg, Opus, libsodium) at this stage.
//   - Do not start implementation code until confirmed by the user.
////////////////////////////////////////////////////////////////////////////////
*/

////////////////////////////////////////////////////////////////////////////////
// INCLUDES & SYSTEM LIBRARIES
////////////////////////////////////////////////////////////////////////////////
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>
#include <bcrypt.h>
#include <commctrl.h>
#include <natupnp.h>
#include <winhttp.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <juice.h>

////////////////////////////////////////////////////////////////////////////////
// COMPILER PRAGMA LINKERS (FOR MSVC / CLANG-CL COMPATIBILITY)
////////////////////////////////////////////////////////////////////////////////
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "avrt.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "winhttp.lib")

////////////////////////////////////////////////////////////////////////////////
// EXTENSIVE NETWORK & HOLE-PUNCHING DIAGNOSTIC LOGGING
////////////////////////////////////////////////////////////////////////////////
// MASTER LOGGING TOGGLE:
// Change this single variable to 1 to enable extensive live console & file logging.
// Change to 0 to completely disable logging and suppress the console window.
#define ENABLE_VERBOSE_NET_LOG 1

static bool g_verbose_net_log = ENABLE_VERBOSE_NET_LOG;
static CRITICAL_SECTION g_net_log_cs;
static bool g_net_log_cs_init = false;
static FILE *g_net_log_file = NULL;

static void net_log(const char *tag, const char *fmt, ...) {
    if (!g_verbose_net_log) return;

    if (!g_net_log_cs_init) {
        InitializeCriticalSection(&g_net_log_cs);
        g_net_log_cs_init = true;
    }

    EnterCriticalSection(&g_net_log_cs);

    SYSTEMTIME st;
    GetLocalTime(&st);

    char time_str[32];
    snprintf(time_str, sizeof(time_str), "%02d:%02d:%02d.%03d",
             st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

    char msg[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    // Output to network_debug.log file
    if (!g_net_log_file) {
        fopen_s(&g_net_log_file, "network_debug.log", "a");
    }
    if (g_net_log_file) {
        fprintf(g_net_log_file, "[%s] [%-12s] %s\n", time_str, tag, msg);
        fflush(g_net_log_file);
    }

    LeaveCriticalSection(&g_net_log_cs);
}

static void net_log_init(void) {
    if (!g_verbose_net_log) return;

    if (!g_net_log_cs_init) {
        InitializeCriticalSection(&g_net_log_cs);
        g_net_log_cs_init = true;
    }

    fopen_s(&g_net_log_file, "network_debug.log", "w");
    net_log("SYSTEM", "==========================================================");
    net_log("SYSTEM", " preAlphaVoiceChat Network Diagnostics Engine ACTIVE");
    net_log("SYSTEM", " Master toggle 'ENABLE_VERBOSE_NET_LOG': 1 (Verbose)");
    net_log("SYSTEM", " Recording all events to 'network_debug.log'");
    net_log("SYSTEM", "==========================================================");
}

static void net_log_cleanup(void) {
    if (!g_verbose_net_log) return;
    if (g_net_log_file) {
        net_log("SYSTEM", "Diagnostics session terminated cleanly.");
        fclose(g_net_log_file);
        g_net_log_file = NULL;
    }
    if (g_net_log_cs_init) {
        DeleteCriticalSection(&g_net_log_cs);
        g_net_log_cs_init = false;
    }
}

////////////////////////////////////////////////////////////////////////////////
// CONSTANTS, PACKET STRUCTS & TYPES
////////////////////////////////////////////////////////////////////////////////
#define VOICE_SAMPLE_RATE       16000
#define VOICE_CHANNELS          1
#define VOICE_BITS_PER_SAMPLE   16
#define VOICE_FRAME_MS          10
#define VOICE_SAMPLES_PER_FRAME ((VOICE_SAMPLE_RATE * VOICE_FRAME_MS) / 1000) // 160 samples
#define VOICE_FRAME_BYTES       (VOICE_SAMPLES_PER_FRAME * (VOICE_BITS_PER_SAMPLE / 8)) // 320 bytes

#define PACKET_MAGIC            0x56434854 // "VCHT" in hex (Voice Chat)
#define MAX_PAYLOAD_SIZE        512

#define PKT_TYPE_AUDIO          0
#define PKT_TYPE_JOIN_REQ       1
#define PKT_TYPE_JOIN_ACK       2
#define PKT_TYPE_HEARTBEAT      3

#pragma pack(push, 1)
typedef struct PacketHeader {
    uint32_t magic;           // PACKET_MAGIC
    uint32_t sequence;        // Monotonically increasing packet sequence number
    uint64_t timestamp_us;    // Microsecond QPC timestamp at transmission
    uint16_t payload_bytes;   // Number of audio bytes in payload
    uint16_t room_id;         // Room identifier
    uint32_t sender_id;       // Unique peer identifier to track who joined the room
    uint8_t  packet_type;     // PKT_TYPE_*
    uint8_t  peer_count;      // Current room peer count (in JOIN_ACK)
    uint16_t reserved;        // Reserved alignment padding
} PacketHeader;

typedef struct VoicePacket {
    PacketHeader header;
    uint8_t payload[MAX_PAYLOAD_SIZE];
} VoicePacket;
#pragma pack(pop)

////////////////////////////////////////////////////////////////////////////////
// TELEMETRY & METRICS ENGINE (PHASE 1)
////////////////////////////////////////////////////////////////////////////////
typedef struct TelemetryState {
    int64_t  qpc_frequency;
    
    // Bandwidth calculation (sliding 1-second window)
    uint64_t last_calc_time_us;
    uint64_t bytes_sent_period;
    uint64_t bytes_recv_period;
    double   kb_per_sec_out;
    double   kb_per_sec_in;

    // Packet sequence & loss tracking
    uint32_t last_recv_seq;
    uint64_t total_packets_sent;
    uint64_t total_packets_recv;
    uint64_t total_packets_lost;
    uint64_t out_of_order_count;
    double   packet_loss_pct;

    // Latency and RFC 3550 Jitter
    double   last_rtt_ms;
    double   jitter_ms;
    uint64_t last_transit_diff_us;

    // Buffer Depth and Overhead
    int      current_buffer_frames;
    double   current_buffer_ms;
    double   crypto_overhead_us;
    double   audio_overhead_us;

    // Live Mic Activity & Peak Level
    int32_t  mic_peak_level;

    // Synchronization
    CRITICAL_SECTION cs;
} TelemetryState;

static TelemetryState g_telemetry;

static void telemetry_init(void) {
    memset(&g_telemetry, 0, sizeof(g_telemetry));
    InitializeCriticalSection(&g_telemetry.cs);

    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    g_telemetry.qpc_frequency = freq.QuadPart;

    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    g_telemetry.last_calc_time_us = (uint64_t)((now.QuadPart * 1000000) / g_telemetry.qpc_frequency);
}

static uint64_t telemetry_now_us(void) {
    if (g_telemetry.qpc_frequency == 0) return 0;
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (uint64_t)((now.QuadPart * 1000000) / g_telemetry.qpc_frequency);
}

static void telemetry_record_send(size_t bytes) {
    EnterCriticalSection(&g_telemetry.cs);
    g_telemetry.bytes_sent_period += bytes;
    g_telemetry.total_packets_sent++;
    LeaveCriticalSection(&g_telemetry.cs);
}

static void telemetry_record_recv(size_t bytes, uint32_t seq, uint64_t send_time_us) {
    uint64_t now_us = telemetry_now_us();

    EnterCriticalSection(&g_telemetry.cs);
    g_telemetry.bytes_recv_period += bytes;
    g_telemetry.total_packets_recv++;

    // Sequence tracking & loss estimation
    if (g_telemetry.total_packets_recv == 1) {
        g_telemetry.last_recv_seq = seq;
    } else {
        if (seq > g_telemetry.last_recv_seq + 1) {
            uint32_t lost = seq - (g_telemetry.last_recv_seq + 1);
            g_telemetry.total_packets_lost += lost;
            g_telemetry.last_recv_seq = seq;
        } else if (seq <= g_telemetry.last_recv_seq) {
            g_telemetry.out_of_order_count++;
        } else {
            g_telemetry.last_recv_seq = seq;
        }
    }

    uint64_t total_expected = g_telemetry.total_packets_recv + g_telemetry.total_packets_lost;
    if (total_expected > 0) {
        g_telemetry.packet_loss_pct = ((double)g_telemetry.total_packets_lost * 100.0) / (double)total_expected;
    }

    // RFC 3550 Jitter calculation: D(i, j) = (R_j - S_j) - (R_i - S_i)
    if (send_time_us > 0 && now_us >= send_time_us) {
        uint64_t transit_us = now_us - send_time_us;
        if (g_telemetry.total_packets_recv > 1) {
            int64_t diff = (int64_t)transit_us - (int64_t)g_telemetry.last_transit_diff_us;
            if (diff < 0) diff = -diff;
            double diff_ms = (double)diff / 1000.0;
            g_telemetry.jitter_ms += (diff_ms - g_telemetry.jitter_ms) / 16.0;
        }
        g_telemetry.last_transit_diff_us = transit_us;
        g_telemetry.last_rtt_ms = (double)transit_us / 1000.0;
    }

    LeaveCriticalSection(&g_telemetry.cs);
}

static void telemetry_update_bandwidth(void) {
    uint64_t now_us = telemetry_now_us();
    EnterCriticalSection(&g_telemetry.cs);

    uint64_t elapsed_us = now_us - g_telemetry.last_calc_time_us;
    if (elapsed_us >= 1000000) { // 1 second window
        double elapsed_sec = (double)elapsed_us / 1000000.0;
        g_telemetry.kb_per_sec_out = ((double)g_telemetry.bytes_sent_period / 1024.0) / elapsed_sec;
        g_telemetry.kb_per_sec_in  = ((double)g_telemetry.bytes_recv_period / 1024.0) / elapsed_sec;
        g_telemetry.bytes_sent_period = 0;
        g_telemetry.bytes_recv_period = 0;
        g_telemetry.last_calc_time_us = now_us;
    }

    LeaveCriticalSection(&g_telemetry.cs);
}

static void telemetry_update_buffer_depth(int frames, double frame_duration_ms) {
    EnterCriticalSection(&g_telemetry.cs);
    g_telemetry.current_buffer_frames = frames;
    g_telemetry.current_buffer_ms = frames * frame_duration_ms;
    LeaveCriticalSection(&g_telemetry.cs);
}

static void telemetry_record_crypto_overhead(double us) {
    EnterCriticalSection(&g_telemetry.cs);
    g_telemetry.crypto_overhead_us = us;
    LeaveCriticalSection(&g_telemetry.cs);
}

static void telemetry_record_mic_level(int32_t level) {
    EnterCriticalSection(&g_telemetry.cs);
    g_telemetry.mic_peak_level = level;
    LeaveCriticalSection(&g_telemetry.cs);
}

static void telemetry_get_snapshot(TelemetryState *out) {
    if (!out) return;
    EnterCriticalSection(&g_telemetry.cs);
    memcpy(out, &g_telemetry, sizeof(TelemetryState));
    LeaveCriticalSection(&g_telemetry.cs);
}

////////////////////////////////////////////////////////////////////////////////
// BCRYPT AES CRYPTOGRAPHY (PHASE 2)
////////////////////////////////////////////////////////////////////////////////
#define AES_KEY_BYTES           32  // AES-256
#define AES_GCM_NONCE_BYTES     12  // Standard GCM 96-bit nonce
#define AES_GCM_TAG_BYTES       16  // 128-bit authentication tag

typedef struct CryptoContext {
    BCRYPT_ALG_HANDLE hAesAlg;
    BCRYPT_ALG_HANDLE hShaAlg;
    BCRYPT_KEY_HANDLE hKey;
    uint8_t           key_buffer[AES_KEY_BYTES];
    bool              initialized;
    CRITICAL_SECTION  cs;
} CryptoContext;

static CryptoContext g_crypto;

static bool crypto_init(const char *passphrase) {
    memset(&g_crypto, 0, sizeof(g_crypto));
    InitializeCriticalSection(&g_crypto.cs);

    if (!passphrase || strlen(passphrase) == 0) {
        passphrase = "preAlphaVoiceChatDefaultKey2026";
    }

    NTSTATUS status;

    // 1. Open SHA-256 algorithm to derive 256-bit key from passphrase
    status = BCryptOpenAlgorithmProvider(&g_crypto.hShaAlg, BCRYPT_SHA256_ALGORITHM, NULL, 0);
    if (!BCRYPT_SUCCESS(status)) return false;

    BCRYPT_HASH_HANDLE hHash = NULL;
    status = BCryptCreateHash(g_crypto.hShaAlg, &hHash, NULL, 0, NULL, 0, 0);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptCloseAlgorithmProvider(g_crypto.hShaAlg, 0);
        return false;
    }

    status = BCryptHashData(hHash, (PUCHAR)passphrase, (ULONG)strlen(passphrase), 0);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptDestroyHash(hHash);
        BCryptCloseAlgorithmProvider(g_crypto.hShaAlg, 0);
        return false;
    }

    status = BCryptFinishHash(hHash, g_crypto.key_buffer, AES_KEY_BYTES, 0);
    BCryptDestroyHash(hHash);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptCloseAlgorithmProvider(g_crypto.hShaAlg, 0);
        return false;
    }

    // 2. Open AES algorithm and set chaining mode to GCM
    status = BCryptOpenAlgorithmProvider(&g_crypto.hAesAlg, BCRYPT_AES_ALGORITHM, NULL, 0);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptCloseAlgorithmProvider(g_crypto.hShaAlg, 0);
        return false;
    }

    status = BCryptSetProperty(g_crypto.hAesAlg,
                               BCRYPT_CHAINING_MODE,
                               (PUCHAR)BCRYPT_CHAIN_MODE_GCM,
                               sizeof(BCRYPT_CHAIN_MODE_GCM),
                               0);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptCloseAlgorithmProvider(g_crypto.hAesAlg, 0);
        BCryptCloseAlgorithmProvider(g_crypto.hShaAlg, 0);
        return false;
    }

    // 3. Generate symmetric key object from SHA-256 hash
    status = BCryptGenerateSymmetricKey(g_crypto.hAesAlg,
                                        &g_crypto.hKey,
                                        NULL,
                                        0,
                                        g_crypto.key_buffer,
                                        AES_KEY_BYTES,
                                        0);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptCloseAlgorithmProvider(g_crypto.hAesAlg, 0);
        BCryptCloseAlgorithmProvider(g_crypto.hShaAlg, 0);
        return false;
    }

    g_crypto.initialized = true;
    return true;
}

static void crypto_cleanup(void) {
    EnterCriticalSection(&g_crypto.cs);
    if (g_crypto.hKey) {
        BCryptDestroyKey(g_crypto.hKey);
        g_crypto.hKey = NULL;
    }
    if (g_crypto.hAesAlg) {
        BCryptCloseAlgorithmProvider(g_crypto.hAesAlg, 0);
        g_crypto.hAesAlg = NULL;
    }
    if (g_crypto.hShaAlg) {
        BCryptCloseAlgorithmProvider(g_crypto.hShaAlg, 0);
        g_crypto.hShaAlg = NULL;
    }
    g_crypto.initialized = false;
    LeaveCriticalSection(&g_crypto.cs);
    DeleteCriticalSection(&g_crypto.cs);
}

// Encrypts payload in-place or into out_buf, authenticating header as Additional Authenticated Data (AAD)
static bool crypto_encrypt_payload(const PacketHeader *header,
                                   const uint8_t *plaintext,
                                   uint32_t plaintext_len,
                                   uint8_t *ciphertext_out,
                                   uint8_t *nonce_out,
                                   uint8_t *tag_out) {
    if (!g_crypto.initialized || !header || !plaintext || !ciphertext_out) return false;

    // Generate unique 12-byte GCM nonce using packet sequence & timestamp
    memset(nonce_out, 0, AES_GCM_NONCE_BYTES);
    memcpy(nonce_out, &header->sequence, sizeof(header->sequence));
    memcpy(nonce_out + 4, &header->timestamp_us, sizeof(uint64_t));

    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
    BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
    authInfo.pbNonce    = nonce_out;
    authInfo.cbNonce    = AES_GCM_NONCE_BYTES;
    authInfo.pbAuthData = (PUCHAR)header;
    authInfo.cbAuthData = sizeof(PacketHeader);
    authInfo.pbTag      = tag_out;
    authInfo.cbTag      = AES_GCM_TAG_BYTES;

    ULONG cbResult = 0;
    NTSTATUS status;

    EnterCriticalSection(&g_crypto.cs);
    status = BCryptEncrypt(g_crypto.hKey,
                           (PUCHAR)plaintext,
                           plaintext_len,
                           &authInfo,
                           NULL,
                           0,
                           (PUCHAR)ciphertext_out,
                           plaintext_len,
                           &cbResult,
                           0);
    LeaveCriticalSection(&g_crypto.cs);

    return BCRYPT_SUCCESS(status);
}

// Decrypts ciphertext in-place or into out_buf, verifying header AAD and auth tag
static bool crypto_decrypt_payload(const PacketHeader *header,
                                   const uint8_t *ciphertext,
                                   uint32_t ciphertext_len,
                                   const uint8_t *nonce,
                                   const uint8_t *tag,
                                   uint8_t *plaintext_out) {
    if (!g_crypto.initialized || !header || !ciphertext || !plaintext_out) return false;

    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
    BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
    authInfo.pbNonce    = (PUCHAR)nonce;
    authInfo.cbNonce    = AES_GCM_NONCE_BYTES;
    authInfo.pbAuthData = (PUCHAR)header;
    authInfo.cbAuthData = sizeof(PacketHeader);
    authInfo.pbTag      = (PUCHAR)tag;
    authInfo.cbTag      = AES_GCM_TAG_BYTES;

    ULONG cbResult = 0;
    NTSTATUS status;

    EnterCriticalSection(&g_crypto.cs);
    status = BCryptDecrypt(g_crypto.hKey,
                           (PUCHAR)ciphertext,
                           ciphertext_len,
                           &authInfo,
                           NULL,
                           0,
                           (PUCHAR)plaintext_out,
                           ciphertext_len,
                           &cbResult,
                           0);
    LeaveCriticalSection(&g_crypto.cs);

    return BCRYPT_SUCCESS(status);
}

////////////////////////////////////////////////////////////////////////////////
// LOW-LATENCY RING BUFFER (PHASE 3)
////////////////////////////////////////////////////////////////////////////////
#define RING_BUFFER_SLOTS       16  // 16 slots * 10ms = 160ms max jitter window
#define TARGET_LATENCY_SLOTS    1   // 1 slot = 10ms optimal floor for ultra-low latency
#define MAX_LATENCY_SLOTS       6   // Max 6 slots (60ms) before catch-up triggers

typedef struct RingBufferSlot {
    uint8_t  pcm_data[VOICE_FRAME_BYTES];
    uint32_t sequence;
    bool     occupied;
} RingBufferSlot;

typedef struct AudioRingBuffer {
    RingBufferSlot slots[RING_BUFFER_SLOTS];
    uint32_t       read_seq;
    uint32_t       write_seq;
    int            occupied_count;
    bool           buffering;       // Wait for initial pre-buffer on stream start
    CRITICAL_SECTION cs;
} AudioRingBuffer;

static AudioRingBuffer g_ring_buf;

static void ring_buffer_init(void) {
    memset(&g_ring_buf, 0, sizeof(g_ring_buf));
    InitializeCriticalSection(&g_ring_buf.cs);
    g_ring_buf.buffering = true;
}

static void ring_buffer_reset(void) {
    EnterCriticalSection(&g_ring_buf.cs);
    for (int i = 0; i < RING_BUFFER_SLOTS; i++) {
        g_ring_buf.slots[i].occupied = false;
    }
    g_ring_buf.occupied_count = 0;
    g_ring_buf.read_seq = 0;
    g_ring_buf.write_seq = 0;
    g_ring_buf.buffering = true;
    LeaveCriticalSection(&g_ring_buf.cs);
}

static void ring_buffer_push(uint32_t seq, const uint8_t *pcm, uint32_t len) {
    if (!pcm || len == 0) return;
    if (len > VOICE_FRAME_BYTES) len = VOICE_FRAME_BYTES;

    EnterCriticalSection(&g_ring_buf.cs);

    // Initial stream synchronization
    if (g_ring_buf.buffering) {
        g_ring_buf.read_seq = seq;
        g_ring_buf.buffering = false;
    }

    // Drop ancient packets that are already past our read window
    if (seq < g_ring_buf.read_seq && (g_ring_buf.read_seq - seq) < 0x80000000) {
        LeaveCriticalSection(&g_ring_buf.cs);
        return;
    }

    int slot_idx = seq % RING_BUFFER_SLOTS;
    RingBufferSlot *slot = &g_ring_buf.slots[slot_idx];

    if (!slot->occupied) {
        g_ring_buf.occupied_count++;
    }

    memcpy(slot->pcm_data, pcm, len);
    if (len < VOICE_FRAME_BYTES) {
        memset(slot->pcm_data + len, 0, VOICE_FRAME_BYTES - len);
    }
    slot->sequence = seq;
    slot->occupied = true;

    if (seq > g_ring_buf.write_seq) {
        g_ring_buf.write_seq = seq;
    }

    // Low-Latency Catch-up: If network burst causes queue to bloat > MAX_LATENCY_SLOTS,
    // advance read_seq to maintain our 10ms - 30ms target floor!
    if (g_ring_buf.occupied_count > MAX_LATENCY_SLOTS) {
        int excess = g_ring_buf.occupied_count - TARGET_LATENCY_SLOTS;
        for (int i = 0; i < excess; i++) {
            int old_slot = g_ring_buf.read_seq % RING_BUFFER_SLOTS;
            if (g_ring_buf.slots[old_slot].occupied) {
                g_ring_buf.slots[old_slot].occupied = false;
                g_ring_buf.occupied_count--;
            }
            g_ring_buf.read_seq++;
        }
    }

    telemetry_update_buffer_depth(g_ring_buf.occupied_count, (double)VOICE_FRAME_MS);
    LeaveCriticalSection(&g_ring_buf.cs);
}

// Pulls 1 frame (10ms) from ring buffer. If underrun, outputs silence and returns false.
static bool ring_buffer_pop(uint8_t *out_pcm, uint32_t len) {
    if (!out_pcm || len < VOICE_FRAME_BYTES) return false;

    EnterCriticalSection(&g_ring_buf.cs);

    if (g_ring_buf.buffering || g_ring_buf.occupied_count == 0) {
        memset(out_pcm, 0, VOICE_FRAME_BYTES);
        LeaveCriticalSection(&g_ring_buf.cs);
        return false;
    }

    int slot_idx = g_ring_buf.read_seq % RING_BUFFER_SLOTS;
    RingBufferSlot *slot = &g_ring_buf.slots[slot_idx];

    bool got_frame = false;
    if (slot->occupied && slot->sequence == g_ring_buf.read_seq) {
        memcpy(out_pcm, slot->pcm_data, VOICE_FRAME_BYTES);
        slot->occupied = false;
        g_ring_buf.occupied_count--;
        g_ring_buf.read_seq++;
        got_frame = true;
    } else {
        // Missing or late packet: Output silence to conceal glitch, advance read_seq
        memset(out_pcm, 0, VOICE_FRAME_BYTES);
        g_ring_buf.read_seq++;
        got_frame = false;
    }

    telemetry_update_buffer_depth(g_ring_buf.occupied_count, (double)VOICE_FRAME_MS);
    LeaveCriticalSection(&g_ring_buf.cs);
    return got_frame;
}

static void ring_buffer_cleanup(void) {
    DeleteCriticalSection(&g_ring_buf.cs);
}

////////////////////////////////////////////////////////////////////////////////
// WASAPI AUDIO ENGINE & DEVICE ENUMERATION (PHASE 4)
////////////////////////////////////////////////////////////////////////////////
// Static GUID definitions to guarantee 100% self-contained linking with zero SDK lib issues
static const GUID LOCAL_CLSID_MMDeviceEnumerator = { 0xbcde0395, 0xe52f, 0x467c, { 0x8e, 0x3d, 0xc4, 0x57, 0x92, 0x91, 0x69, 0x2e } };
static const GUID LOCAL_IID_IMMDeviceEnumerator  = { 0xa95664d2, 0x9614, 0x4f35, { 0xa7, 0x46, 0xde, 0x8d, 0xb6, 0x36, 0x17, 0xe6 } };
static const GUID LOCAL_IID_IAudioClient         = { 0x1cb9ad4c, 0xdbfa, 0x4c32, { 0xb1, 0x78, 0xc2, 0xf5, 0x68, 0xa7, 0x03, 0xb2 } };
static const GUID LOCAL_IID_IAudioCaptureClient  = { 0xc8adbd64, 0xe71e, 0x48a0, { 0xa4, 0xde, 0x18, 0x5c, 0x39, 0x5c, 0xd3, 0x17 } };
static const GUID LOCAL_IID_IAudioRenderClient   = { 0xf294acfc, 0x3146, 0x4483, { 0xa7, 0xbf, 0xad, 0xdc, 0xa7, 0xc2, 0x60, 0xe2 } };
static const PROPERTYKEY LOCAL_PKEY_Device_FriendlyName = { { 0xa45c254e, 0xdf1c, 0x4efd, { 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0 } }, 14 };

#define MAX_AUDIO_DEVICES 16

typedef struct AudioDeviceInfo {
    WCHAR id[128];
    char  name[128];
    bool  is_default;
} AudioDeviceInfo;

typedef struct AudioDeviceList {
    AudioDeviceInfo devices[MAX_AUDIO_DEVICES];
    int             count;
    int             default_index;
} AudioDeviceList;

typedef struct AudioEngine {
    IMMDeviceEnumerator *pEnumerator;
    IMMDevice           *pCaptureDevice;
    IMMDevice           *pRenderDevice;
    IAudioClient        *pCaptureAudioClient;
    IAudioClient        *pRenderAudioClient;
    IAudioCaptureClient *pCaptureClient;
    IAudioRenderClient  *pRenderClient;

    HANDLE              hCaptureEvent;
    HANDLE              hRenderEvent;
    HANDLE              hCaptureThread;
    HANDLE              hRenderThread;

    UINT32              capture_buffer_frames;
    UINT32              render_buffer_frames;
    WAVEFORMATEX        wfx;

    bool                is_running;
    bool                loopback_test_mode;
    uint32_t            capture_seq;
    int                 playback_volume;

    AudioDeviceList     capture_devices;
    AudioDeviceList     render_devices;
} AudioEngine;

static AudioEngine g_audio;

static void audio_enumerate_devices(EDataFlow dataFlow, AudioDeviceList *list) {
    if (!list || !g_audio.pEnumerator) return;
    memset(list, 0, sizeof(AudioDeviceList));
    list->default_index = -1;

    IMMDevice *pDefaultDevice = NULL;
    LPWSTR pDefaultId = NULL;
    if (SUCCEEDED(g_audio.pEnumerator->lpVtbl->GetDefaultAudioEndpoint(g_audio.pEnumerator, dataFlow, eConsole, &pDefaultDevice))) {
        pDefaultDevice->lpVtbl->GetId(pDefaultDevice, &pDefaultId);
    }

    IMMDeviceCollection *pCollection = NULL;
    HRESULT hr = g_audio.pEnumerator->lpVtbl->EnumAudioEndpoints(g_audio.pEnumerator, dataFlow, DEVICE_STATE_ACTIVE, &pCollection);
    if (FAILED(hr) || !pCollection) {
        if (pDefaultDevice) pDefaultDevice->lpVtbl->Release(pDefaultDevice);
        return;
    }

    UINT count = 0;
    pCollection->lpVtbl->GetCount(pCollection, &count);
    if (count > MAX_AUDIO_DEVICES) count = MAX_AUDIO_DEVICES;

    for (UINT i = 0; i < count; i++) {
        IMMDevice *pDevice = NULL;
        if (SUCCEEDED(pCollection->lpVtbl->Item(pCollection, i, &pDevice))) {
            LPWSTR pId = NULL;
            pDevice->lpVtbl->GetId(pDevice, &pId);
            if (pId) {
                wcsncpy_s(list->devices[list->count].id, 128, pId, _TRUNCATE);
                if (pDefaultId && wcscmp(pId, pDefaultId) == 0) {
                    list->devices[list->count].is_default = true;
                    list->default_index = list->count;
                }
                CoTaskMemFree(pId);
            }

            IPropertyStore *pProps = NULL;
            if (SUCCEEDED(pDevice->lpVtbl->OpenPropertyStore(pDevice, STGM_READ, &pProps))) {
                PROPVARIANT varName;
                PropVariantInit(&varName);
                if (SUCCEEDED(pProps->lpVtbl->GetValue(pProps, &LOCAL_PKEY_Device_FriendlyName, &varName))) {
                    if (varName.vt == VT_LPWSTR && varName.pwszVal) {
                        WideCharToMultiByte(CP_UTF8, 0, varName.pwszVal, -1, list->devices[list->count].name, 128, NULL, NULL);
                    }
                    PropVariantClear(&varName);
                }
                pProps->lpVtbl->Release(pProps);
            }

            if (strlen(list->devices[list->count].name) == 0) {
                snprintf(list->devices[list->count].name, 128, "Audio Device %u", i + 1);
            }

            list->count++;
            pDevice->lpVtbl->Release(pDevice);
        }
    }

    pCollection->lpVtbl->Release(pCollection);
    if (pDefaultId) CoTaskMemFree(pDefaultId);
    if (pDefaultDevice) pDefaultDevice->lpVtbl->Release(pDefaultDevice);

    if (list->default_index == -1 && list->count > 0) {
        list->default_index = 0;
        list->devices[0].is_default = true;
    }
}

////////////////////////////////////////////////////////////////////////////////
// WINSOCK2 UDP NETWORK ENGINE & NAT TRAVERSAL (PHASE 5)
////////////////////////////////////////////////////////////////////////////////
#define DEFAULT_RELAY_PORT      7777
#define MAX_RELAY_CLIENTS       32
#define CLIENT_TIMEOUT_US       10000000 // 10 seconds timeout for inactive peers

static void get_local_ip(char *out_ip, size_t max_len) {
    char hostname[256] = {0};
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        struct hostent *he = gethostbyname(hostname);
        if (he && he->h_addr_list) {
            for (int i = 0; he->h_addr_list[i] != NULL; i++) {
                struct in_addr addr;
                memcpy(&addr, he->h_addr_list[i], sizeof(struct in_addr));
                const char *ip_str = inet_ntoa(addr);
                if (ip_str && strncmp(ip_str, "127.", 4) != 0 && strncmp(ip_str, "169.254.", 8) != 0) {
                    strncpy_s(out_ip, max_len, ip_str, _TRUNCATE);
                    return;
                }
            }
        }
    }
    strncpy_s(out_ip, max_len, "127.0.0.1", _TRUNCATE);
}

#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif

// Windows Winsock ICMP Port Unreachable fix (prevents error 10054 on UDP hole punching)
static void disable_udp_connreset(SOCKET s) {
    DWORD dwBytesReturned = 0;
    BOOL bNewBehavior = FALSE;
    WSAIoctl(s, SIO_UDP_CONNRESET, &bNewBehavior, sizeof(bNewBehavior), NULL, 0, &dwBytesReturned, NULL, NULL);
}

// Cryptographically strong session token generator
static void generate_session_token(char *out_token, size_t max_len) {
    uint32_t rnd = 0;
    if (BCryptGenRandom(NULL, (PUCHAR)&rnd, sizeof(rnd), BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0 && rnd != 0) {
        snprintf(out_token, max_len, "%08X", (unsigned)rnd);
    } else {
        uint64_t perf = 0;
        LARGE_INTEGER qpc;
        if (QueryPerformanceCounter(&qpc)) perf = (uint64_t)qpc.QuadPart;
        rnd = (uint32_t)(perf ^ ((uint64_t)GetCurrentProcessId() << 16) ^ (uint64_t)GetTickCount());
        snprintf(out_token, max_len, "%08X", (unsigned)rnd);
    }
}

// Pure Winsock MQTT Signaling Engine (TCP Port 1883)
static SOCKET mqtt_connect(const char *host, int port, const char *client_id) {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        net_log("MQTT", "FAIL: socket(TCP) error: %d", WSAGetLastError());
        return INVALID_SOCKET;
    }

    struct hostent *he = gethostbyname(host);
    if (!he || !he->h_addr_list[0]) {
        net_log("MQTT", "FAIL: DNS lookup failed for broker '%s'", host);
        closesocket(s);
        return INVALID_SOCKET;
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons((u_short)port);
    memcpy(&serv_addr.sin_addr, he->h_addr_list[0], sizeof(struct in_addr));

    DWORD to = 1500;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&to, sizeof(to));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&to, sizeof(to));

    if (connect(s, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == SOCKET_ERROR) {
        net_log("MQTT", "FAIL: TCP connect to %s:%d failed (WSA error: %d)", host, port, WSAGetLastError());
        closesocket(s);
        return INVALID_SOCKET;
    }

    uint8_t pkt[128];
    int id_len = (int)strlen(client_id);
    int rem_len = 10 + 2 + id_len;

    int idx = 0;
    pkt[idx++] = 0x10; // CONNECT
    pkt[idx++] = (uint8_t)rem_len;
    pkt[idx++] = 0x00; pkt[idx++] = 0x04; pkt[idx++] = 'M'; pkt[idx++] = 'Q'; pkt[idx++] = 'T'; pkt[idx++] = 'T';
    pkt[idx++] = 0x04; // Protocol level (MQTT 3.1.1)
    pkt[idx++] = 0x02; // Clean session
    pkt[idx++] = 0x00; pkt[idx++] = 0x3C; // Keepalive 60s
    pkt[idx++] = (uint8_t)(id_len >> 8); pkt[idx++] = (uint8_t)(id_len & 0xFF);
    memcpy(pkt + idx, client_id, id_len);
    idx += id_len;

    if (send(s, (const char*)pkt, idx, 0) != idx) {
        net_log("MQTT", "FAIL: send(CONNECT) failed to %s:%d", host, port);
        closesocket(s);
        return INVALID_SOCKET;
    }

    uint8_t ack[4];
    int r = recv(s, (char*)ack, sizeof(ack), 0);
    if (r < 4 || ack[0] != 0x20 || ack[3] != 0x00) {
        net_log("MQTT", "FAIL: Invalid CONNACK from %s:%d (recv=%d, byte0=0x%02X, retcode=%d)",
                host, port, r, (r > 0 ? ack[0] : 0), (r >= 4 ? ack[3] : -1));
        closesocket(s);
        return INVALID_SOCKET;
    }

    net_log("MQTT", "Connected to MQTT broker %s:%d (Client ID: %s)", host, port, client_id);
    return s;
}

static SOCKET mqtt_connect_default(const char *client_id, char *out_broker_used, size_t broker_len) {
    net_log("MQTT", "Attempting primary broker EMQX (broker.emqx.io:1883)...");
    SOCKET s = mqtt_connect("broker.emqx.io", 1883, client_id);
    if (s != INVALID_SOCKET) {
        if (out_broker_used && broker_len > 0) {
            strncpy_s(out_broker_used, broker_len, "broker.emqx.io:1883", _TRUNCATE);
        }
        return s;
    }
    net_log("MQTT", "EMQX failed. Trying backup broker HiveMQ (broker.hivemq.com:1883)...");
    s = mqtt_connect("broker.hivemq.com", 1883, client_id);
    if (s != INVALID_SOCKET) {
        if (out_broker_used && broker_len > 0) {
            strncpy_s(out_broker_used, broker_len, "broker.hivemq.com:1883", _TRUNCATE);
        }
        return s;
    }
    net_log("MQTT", "HiveMQ failed. Trying backup broker Mosquitto (test.mosquitto.org:1883)...");
    s = mqtt_connect("test.mosquitto.org", 1883, client_id);
    if (s != INVALID_SOCKET) {
        if (out_broker_used && broker_len > 0) {
            strncpy_s(out_broker_used, broker_len, "test.mosquitto.org:1883", _TRUNCATE);
        }
        return s;
    }
    net_log("MQTT", "CRITICAL: All MQTT signaling brokers failed to connect!");
    return INVALID_SOCKET;
}

static bool mqtt_publish(SOCKET s, const char *topic, const char *payload, bool retain) {
    if (s == INVALID_SOCKET || !topic || !payload) return false;
    int t_len = (int)strlen(topic);
    int p_len = (int)strlen(payload);
    int rem_len = 2 + t_len + p_len;

    int total_alloc = 5 + rem_len;
    uint8_t *pkt = (uint8_t*)malloc(total_alloc);
    if (!pkt) return false;

    int idx = 0;
    pkt[idx++] = retain ? 0x31 : 0x30; // PUBLISH (QoS 0, retain flag optional)

    // Standard MQTT variable-length integer encoding
    int rem = rem_len;
    do {
        uint8_t encoded_byte = (uint8_t)(rem % 128);
        rem /= 128;
        if (rem > 0) encoded_byte |= 0x80;
        pkt[idx++] = encoded_byte;
    } while (rem > 0);

    pkt[idx++] = (uint8_t)(t_len >> 8);
    pkt[idx++] = (uint8_t)(t_len & 0xFF);
    memcpy(pkt + idx, topic, t_len);
    idx += t_len;

    memcpy(pkt + idx, payload, p_len);
    idx += p_len;

    int total_sent = 0;
    while (total_sent < idx) {
        int sres = send(s, (const char*)pkt + total_sent, idx - total_sent, 0);
        if (sres <= 0) {
            free(pkt);
            net_log("MQTT", "PUBLISH [%s] (retain=%d, status=FAIL, len=%d)", topic, retain ? 1 : 0, p_len);
            return false;
        }
        total_sent += sres;
    }

    free(pkt);
    net_log("MQTT", "PUBLISH [%s] (retain=%d, status=OK, len=%d)", topic, retain ? 1 : 0, p_len);
    return true;
}

static bool mqtt_send_ping(SOCKET s) {
    if (s == INVALID_SOCKET) return false;
    uint8_t ping[] = { 0xC0, 0x00 };
    return send(s, (const char*)ping, 2, 0) == 2;
}

static bool mqtt_subscribe(SOCKET s, const char *topic) {
    if (s == INVALID_SOCKET || !topic) return false;
    int t_len = (int)strlen(topic);
    int rem_len = 2 + 2 + t_len + 1;

    uint8_t pkt[128];
    int idx = 0;
    pkt[idx++] = 0x82; // SUBSCRIBE
    pkt[idx++] = (uint8_t)rem_len;
    pkt[idx++] = 0x00; pkt[idx++] = 0x01; // Packet ID: 1
    pkt[idx++] = (uint8_t)(t_len >> 8); pkt[idx++] = (uint8_t)(t_len & 0xFF);
    memcpy(pkt + idx, topic, t_len); idx += t_len;
    pkt[idx++] = 0x00; // Requested QoS: 0

    if (send(s, (const char*)pkt, idx, 0) != idx) {
        net_log("MQTT", "FAIL: send(SUBSCRIBE) failed for topic '%s'", topic);
        return false;
    }

    uint8_t ack[5];
    int r = recv(s, (char*)ack, sizeof(ack), 0);
    bool ok = (r >= 3 && ack[0] == 0x90);
    net_log("MQTT", "SUBSCRIBE [%s] -> %s (SUBACK byte0=0x%02X)", topic, ok ? "SUCCESS" : "FAILED", (r > 0 ? ack[0] : 0));
    return ok;
}

static int mqtt_parse_publish(const uint8_t *pkt, int len, char *out_topic, size_t max_topic, char *out_payload, size_t max_payload) {
    if (!pkt || len < 4) return 0;
    int p_start = -1;
    for (int i = 0; i <= len - 4; i++) {
        if ((pkt[i] & 0xF0) == 0x30) {
            p_start = i;
            break;
        }
    }
    if (p_start == -1) return len; // Skip non-publish bytes in stream

    int qos = (pkt[p_start] >> 1) & 0x03;
    int idx = p_start + 1;
    int rem_len = 0;
    int mult = 1;
    while (idx < len) {
        uint8_t b = pkt[idx++];
        rem_len += (b & 0x7F) * mult;
        if ((b & 0x80) == 0) break;
        mult *= 128;
    }
    if (idx + 2 > len) return 0; // Incomplete header

    int t_len = ((int)pkt[idx] << 8) | (int)pkt[idx + 1];
    idx += 2;
    if (idx + t_len > len) return 0; // Incomplete topic

    if (out_topic && max_topic > 0) {
        int copy_t = (t_len < (int)max_topic - 1) ? t_len : (int)max_topic - 1;
        memcpy(out_topic, pkt + idx, copy_t);
        out_topic[copy_t] = '\0';
    }
    idx += t_len;

    if (qos > 0) {
        idx += 2; // skip 2-byte Packet Identifier for QoS 1 or 2
    }

    int pay_len = rem_len - 2 - t_len - (qos > 0 ? 2 : 0);
    if (pay_len < 0) return 0;
    if (idx + pay_len > len) {
        return 0; // Incomplete payload, wait for more data
    }

    if (out_payload && max_payload > 0) {
        int copy_p = (pay_len < (int)max_payload - 1) ? pay_len : (int)max_payload - 1;
        memcpy(out_payload, pkt + idx, copy_p);
        out_payload[copy_p] = '\0';
    }
    return idx + pay_len;
}

#pragma pack(push, 1)
typedef struct WirePacket {
    PacketHeader header;
    uint8_t      nonce[AES_GCM_NONCE_BYTES];
    uint8_t      tag[AES_GCM_TAG_BYTES];
    uint8_t      ciphertext[VOICE_FRAME_BYTES];
} WirePacket;
#pragma pack(pop)

/// Peer Presence Tracking
#define MAX_ROOM_PEERS          16

typedef struct PeerPresence {
    uint32_t sender_id;
    uint64_t last_seen_us;
} PeerPresence;

// libjuice RFC 8445 ICE Session State
typedef struct IceSession {
    juice_agent_t       *agent;
    bool                is_host;
    uint16_t            room_id;
    char                session_token[32];
    char                local_sdp[JUICE_MAX_SDP_STRING_LEN];
    bool                local_sdp_ready;
    bool                remote_sdp_set;
    char                queued_candidates[32][JUICE_MAX_CANDIDATE_SDP_STRING_LEN];
    int                 queued_candidate_count;
    char                local_candidates[32][JUICE_MAX_CANDIDATE_SDP_STRING_LEN];
    int                 local_candidate_count;
    bool                gathering_done;
    juice_state_t       state;
    char                selected_local[JUICE_MAX_ADDRESS_STRING_LEN];
    char                selected_remote[JUICE_MAX_ADDRESS_STRING_LEN];
    char                selected_local_cand[JUICE_MAX_CANDIDATE_SDP_STRING_LEN];
    char                selected_remote_cand[JUICE_MAX_CANDIDATE_SDP_STRING_LEN];
    SOCKET              mqtt_sock;
    HANDLE              hSignalingThread;
    bool                is_running;
    bool                is_connected;
    uint32_t            my_sender_id;
    PeerPresence        peers[MAX_ROOM_PEERS];
    CRITICAL_SECTION    cs;
} IceSession;

static IceSession g_ice;

static void ice_record_peer(uint32_t sid, uint64_t now_us) {
    if (sid == 0 || sid == g_ice.my_sender_id) return;
    EnterCriticalSection(&g_ice.cs);
    int found = -1;
    int free_slot = -1;
    for (int i = 0; i < MAX_ROOM_PEERS; i++) {
        if (g_ice.peers[i].sender_id == sid) {
            found = i;
            break;
        } else if (g_ice.peers[i].sender_id == 0 && free_slot == -1) {
            free_slot = i;
        }
    }
    if (found != -1) {
        g_ice.peers[found].last_seen_us = now_us;
    } else if (free_slot != -1) {
        g_ice.peers[free_slot].sender_id = sid;
        g_ice.peers[free_slot].last_seen_us = now_us;
    }
    LeaveCriticalSection(&g_ice.cs);
}

static int ice_get_active_peers(void) {
    if (!g_ice.is_running) return 0;
    uint64_t now_us = telemetry_now_us();
    int active = 0;
    EnterCriticalSection(&g_ice.cs);
    for (int i = 0; i < MAX_ROOM_PEERS; i++) {
        if (g_ice.peers[i].sender_id != 0) {
            if (now_us - g_ice.peers[i].last_seen_us <= 3500000) {
                active++;
            } else {
                g_ice.peers[i].sender_id = 0;
            }
        }
    }
    LeaveCriticalSection(&g_ice.cs);
    return active;
}

// libjuice Log Callback
static void on_juice_log(juice_log_level_t level, const char *message) {
    const char *lvl_str = "INFO";
    switch (level) {
        case JUICE_LOG_LEVEL_VERBOSE: lvl_str = "VERBOSE"; break;
        case JUICE_LOG_LEVEL_DEBUG:   lvl_str = "DEBUG";   break;
        case JUICE_LOG_LEVEL_INFO:    lvl_str = "INFO";    break;
        case JUICE_LOG_LEVEL_WARN:    lvl_str = "WARN";    break;
        case JUICE_LOG_LEVEL_ERROR:   lvl_str = "ERROR";   break;
        case JUICE_LOG_LEVEL_FATAL:   lvl_str = "FATAL";   break;
        default: break;
    }
    net_log("JUICE", "[%s] %s", lvl_str, message);
}

// libjuice State Changed Callback
static void on_juice_state_changed(juice_agent_t *agent, juice_state_t state, void *user_ptr) {
    (void)user_ptr;
    EnterCriticalSection(&g_ice.cs);
    g_ice.state = state;
    net_log("ICE", "State changed -> %s (%d)", juice_state_to_string(state), state);

    if (state == JUICE_STATE_CONNECTED || state == JUICE_STATE_COMPLETED) {
        g_ice.is_connected = true;
        juice_get_selected_addresses(agent, g_ice.selected_local, sizeof(g_ice.selected_local),
                                     g_ice.selected_remote, sizeof(g_ice.selected_remote));
        juice_get_selected_candidates(agent, g_ice.selected_local_cand, sizeof(g_ice.selected_local_cand),
                                      g_ice.selected_remote_cand, sizeof(g_ice.selected_remote_cand));
        net_log("ICE", "*** ICE CONNECTION ESTABLISHED! ***");
        net_log("ICE", "Selected Local:  %s (%s)", g_ice.selected_local, g_ice.selected_local_cand);
        net_log("ICE", "Selected Remote: %s (%s)", g_ice.selected_remote, g_ice.selected_remote_cand);

        // Transmit an initial control handshake packet
        WirePacket init_pkt;
        memset(&init_pkt, 0, sizeof(init_pkt));
        init_pkt.header.magic = PACKET_MAGIC;
        init_pkt.header.packet_type = PKT_TYPE_JOIN_ACK;
        init_pkt.header.room_id = g_ice.room_id;
        init_pkt.header.sender_id = g_ice.my_sender_id;
        init_pkt.header.timestamp_us = telemetry_now_us();
        juice_send(agent, (const char*)&init_pkt, sizeof(PacketHeader));
    } else if (state == JUICE_STATE_FAILED) {
        g_ice.is_connected = false;
        net_log("ICE", "CRITICAL: ICE Connection FAILED (all candidate pairs exhausted).");
    } else if (state == JUICE_STATE_DISCONNECTED) {
        g_ice.is_connected = false;
        net_log("ICE", "ICE Disconnected.");
    }
    LeaveCriticalSection(&g_ice.cs);
}

// libjuice Candidate Gathered Callback
static void on_juice_candidate(juice_agent_t *agent, const char *sdp, void *user_ptr) {
    (void)agent;
    (void)user_ptr;
    EnterCriticalSection(&g_ice.cs);
    net_log("ICE-CAND", "Gathered candidate from libjuice: %s", sdp);

    // Completely disable LAN connection: drop local host candidates to enforce WAN UDP hole punching
    if (strstr(sdp, "typ host") != NULL) {
        net_log("ICE-CAND", "--> [LAN DISABLED] Filtered out local host candidate to test WAN hole punching: %s", sdp);
        LeaveCriticalSection(&g_ice.cs);
        return;
    }

    if (g_ice.local_candidate_count < 32) {
        strncpy_s(g_ice.local_candidates[g_ice.local_candidate_count],
                  sizeof(g_ice.local_candidates[0]), sdp, _TRUNCATE);
        g_ice.local_candidate_count++;
        net_log("ICE-CAND", "--> Queued local candidate #%d for signaling: %s",
                g_ice.local_candidate_count - 1, sdp);
    }
    LeaveCriticalSection(&g_ice.cs);
}

// libjuice Gathering Done Callback
static void on_juice_gathering_done(juice_agent_t *agent, void *user_ptr) {
    (void)agent;
    (void)user_ptr;
    EnterCriticalSection(&g_ice.cs);
    g_ice.gathering_done = true;
    net_log("ICE-CAND", "Local candidate gathering COMPLETED (%d candidates gathered)", g_ice.local_candidate_count);
    LeaveCriticalSection(&g_ice.cs);
}

// libjuice Message Received Callback
static void on_juice_recv(juice_agent_t *agent, const char *data, size_t size, void *user_ptr) {
    (void)agent;
    (void)user_ptr;
    if (!data || size < sizeof(PacketHeader)) return;
    WirePacket *packet = (WirePacket*)data;
    if (packet->header.magic != PACKET_MAGIC) return;

    // Record peer presence
    if (packet->header.sender_id != 0) {
        ice_record_peer(packet->header.sender_id, telemetry_now_us());
    }

    // Control packets
    if (packet->header.packet_type == PKT_TYPE_JOIN_REQ ||
        packet->header.packet_type == PKT_TYPE_JOIN_ACK ||
        packet->header.packet_type == PKT_TYPE_HEARTBEAT) {
        return;
    }

    if (size < sizeof(WirePacket)) return;

    // Audio frame decryption
    uint8_t decrypted_pcm[VOICE_FRAME_BYTES];
    uint64_t t0 = telemetry_now_us();
    bool dec_ok = crypto_decrypt_payload(&packet->header,
                                         packet->ciphertext,
                                         VOICE_FRAME_BYTES,
                                         packet->nonce,
                                         packet->tag,
                                         decrypted_pcm);
    uint64_t t1 = telemetry_now_us();
    telemetry_record_crypto_overhead((double)(t1 - t0));

    if (!dec_ok) {
        return;
    }

    telemetry_record_recv(size, packet->header.sequence, packet->header.timestamp_us);
    ring_buffer_push(packet->header.sequence, decrypted_pcm, VOICE_FRAME_BYTES);
}


// ICE Signaling Thread (MQTT Pub/Sub for SDP & Candidate Exchange)
static DWORD WINAPI ice_signaling_thread(LPVOID param) {
    (void)param;
    char my_role[8];
    char rem_role[8];
    if (g_ice.is_host) {
        strcpy_s(my_role, sizeof(my_role), "host");
        strcpy_s(rem_role, sizeof(rem_role), "peer");
    } else {
        strcpy_s(my_role, sizeof(my_role), "peer");
        strcpy_s(rem_role, sizeof(rem_role), "host");
    }

    char sub_topic[64];
    snprintf(sub_topic, sizeof(sub_topic), "vchat/%s/%s/#", g_ice.session_token, rem_role);

    char my_desc_topic[64];
    snprintf(my_desc_topic, sizeof(my_desc_topic), "vchat/%s/%s/desc", g_ice.session_token, my_role);

    char my_cand_prefix[64];
    snprintf(my_cand_prefix, sizeof(my_cand_prefix), "vchat/%s/%s/cand", g_ice.session_token, my_role);

    char my_done_topic[64];
    snprintf(my_done_topic, sizeof(my_done_topic), "vchat/%s/%s/done", g_ice.session_token, my_role);

    char room_token_topic[64];
    snprintf(room_token_topic, sizeof(room_token_topic), "vchat/room_%u/token", g_ice.room_id);

    net_log("ICE-SIGNAL", "Signaling thread started for session '%s' (Role: %s)", g_ice.session_token, my_role);
    net_log("ICE-SIGNAL", "Subscribing to: '%s'", sub_topic);

    int published_cand_idx = 0;
    bool published_desc = false;
    bool published_done = false;
    bool published_room_token = false;
    uint64_t last_ping_us = telemetry_now_us();

    while (g_ice.is_running) {
        // Connect / Reconnect MQTT if needed
        if (g_ice.mqtt_sock == INVALID_SOCKET) {
            char client_id[64];
            snprintf(client_id, sizeof(client_id), "vci_%s_%04x",
                     my_role, (uint16_t)(rand() & 0xFFFF));

            char broker_used[64] = {0};
            g_ice.mqtt_sock = mqtt_connect_default(client_id, broker_used, sizeof(broker_used));
            if (g_ice.mqtt_sock != INVALID_SOCKET) {
                net_log("ICE-SIGNAL", "Connected to MQTT broker: %s", broker_used);
                mqtt_subscribe(g_ice.mqtt_sock, sub_topic);
                if (!g_ice.is_host) {
                    mqtt_subscribe(g_ice.mqtt_sock, room_token_topic);
                }
                DWORD recv_to = 50; // 50ms timeout for non-blocking loop
                setsockopt(g_ice.mqtt_sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&recv_to, sizeof(recv_to));
                last_ping_us = telemetry_now_us();
                published_desc = false;
                published_cand_idx = 0;
                published_done = false;
                published_room_token = false;
            } else {
                net_log("ICE-SIGNAL", "WARNING: MQTT broker connection failed. Retrying in 1s...");
                for (int s = 0; s < 20 && g_ice.is_running; s++) {
                    Sleep(50);
                }
                continue;
            }
        }

        // 0. Host publishes session token to room discovery topic
        if (g_ice.is_host && !published_room_token) {
            if (mqtt_publish(g_ice.mqtt_sock, room_token_topic, g_ice.session_token, true)) {
                published_room_token = true;
                net_log("ICE-SIGNAL", "Published room discovery token to [%s]: '%s'", room_token_topic, g_ice.session_token);
            }
        }

        // 1. Publish local SDP description once ready
        if (!published_desc && g_ice.local_sdp_ready) {
            EnterCriticalSection(&g_ice.cs);
            char sdp_copy[JUICE_MAX_SDP_STRING_LEN];
            strncpy_s(sdp_copy, sizeof(sdp_copy), g_ice.local_sdp, _TRUNCATE);
            LeaveCriticalSection(&g_ice.cs);

            if (mqtt_publish(g_ice.mqtt_sock, my_desc_topic, sdp_copy, true)) {
                published_desc = true;
                net_log("ICE-SIGNAL", "Published local SDP description to [%s] (retained)", my_desc_topic);
            }
        }

        // 2. Publish newly gathered local candidates
        int current_cand_count = 0;
        EnterCriticalSection(&g_ice.cs);
        current_cand_count = g_ice.local_candidate_count;
        LeaveCriticalSection(&g_ice.cs);

        while (published_cand_idx < current_cand_count) {
            EnterCriticalSection(&g_ice.cs);
            char cand_copy[JUICE_MAX_CANDIDATE_SDP_STRING_LEN];
            strncpy_s(cand_copy, sizeof(cand_copy), g_ice.local_candidates[published_cand_idx], _TRUNCATE);
            LeaveCriticalSection(&g_ice.cs);

            char cand_topic[80];
            snprintf(cand_topic, sizeof(cand_topic), "%s/%d", my_cand_prefix, published_cand_idx);
            if (mqtt_publish(g_ice.mqtt_sock, cand_topic, cand_copy, true)) {
                net_log("ICE-SIGNAL", "Published candidate #%d to [%s]: '%s'", published_cand_idx, cand_topic, cand_copy);
                published_cand_idx++;
            } else {
                break;
            }
        }

        // 3. Publish gathering done
        if (!published_done && g_ice.gathering_done && published_cand_idx >= current_cand_count) {
            if (mqtt_publish(g_ice.mqtt_sock, my_done_topic, "DONE", true)) {
                published_done = true;
                net_log("ICE-SIGNAL", "Published gathering DONE to [%s]", my_done_topic);
            }
        }

        // 4. Receive incoming MQTT messages from remote peer
        uint8_t buf[8192];
        int bytes = recv(g_ice.mqtt_sock, (char*)buf, sizeof(buf) - 1, 0);
        if (!g_ice.is_running) break;

        if (bytes <= 0) {
            int err = (bytes < 0) ? WSAGetLastError() : 0;
            if (bytes == 0 || (err != WSAETIMEDOUT && err != WSAEWOULDBLOCK)) {
                net_log("ICE-SIGNAL", "MQTT connection dropped (bytes=%d, WSA err=%d). Reconnecting...", bytes, err);
                closesocket(g_ice.mqtt_sock);
                g_ice.mqtt_sock = INVALID_SOCKET;
                for (int s = 0; s < 5 && g_ice.is_running; s++) {
                    Sleep(50);
                }
                continue;
            }
        } else {
            buf[bytes] = '\0';
            int offset = 0;
            while (offset < bytes && g_ice.is_running) {
                char r_topic[128] = {0};
                char r_payload[4096] = {0};
                int consumed = mqtt_parse_publish(buf + offset, bytes - offset,
                                                  r_topic, sizeof(r_topic),
                                                  r_payload, sizeof(r_payload));
                if (consumed <= 0) break;
                offset += consumed;

                if (r_topic[0] == '\0') continue; // non-publish packet

                net_log("ICE-SIGNAL", "RECV MQTT msg on [%s] (%d bytes payload)", r_topic, (int)strlen(r_payload));

                // Check if message is room discovery token (for joiners without manual invite code)
                if (strstr(r_topic, "/token") != NULL && !g_ice.is_host) {
                    if (strlen(r_payload) > 0 && strcmp(g_ice.session_token, r_payload) != 0) {
                        EnterCriticalSection(&g_ice.cs);
                        strncpy_s(g_ice.session_token, sizeof(g_ice.session_token), r_payload, _TRUNCATE);
                        snprintf(sub_topic, sizeof(sub_topic), "vchat/%s/host/#", g_ice.session_token);
                        snprintf(my_desc_topic, sizeof(my_desc_topic), "vchat/%s/peer/desc", g_ice.session_token);
                        snprintf(my_cand_prefix, sizeof(my_cand_prefix), "vchat/%s/peer/cand", g_ice.session_token);
                        snprintf(my_done_topic, sizeof(my_done_topic), "vchat/%s/peer/done", g_ice.session_token);
                        published_desc = false;
                        published_cand_idx = 0;
                        LeaveCriticalSection(&g_ice.cs);
                        mqtt_subscribe(g_ice.mqtt_sock, sub_topic);
                        net_log("ICE-SIGNAL", "Discovered host session token '%s'! Subscribed to [%s]", g_ice.session_token, sub_topic);
                    }
                }
                // Check if message is remote description
                else if (strstr(r_topic, "/desc") != NULL) {
                    EnterCriticalSection(&g_ice.cs);
                    if (!g_ice.remote_sdp_set && strlen(r_payload) > 0) {
                        net_log("ICE-SIGNAL", "Setting remote SDP description (%d bytes)...", (int)strlen(r_payload));
                        int res = juice_set_remote_description(g_ice.agent, r_payload);
                        if (res == JUICE_ERR_SUCCESS) {
                            g_ice.remote_sdp_set = true;
                            net_log("ICE-SIGNAL", "Remote SDP description set successfully!");

                            // Flush any candidates that were queued before description arrived
                            for (int q = 0; q < g_ice.queued_candidate_count; q++) {
                                if (strstr(g_ice.queued_candidates[q], "typ host") != NULL) continue;
                                net_log("ICE-SIGNAL", "Adding queued remote candidate #%d: '%s'", q, g_ice.queued_candidates[q]);
                                juice_add_remote_candidate(g_ice.agent, g_ice.queued_candidates[q]);
                            }
                            g_ice.queued_candidate_count = 0;
                        } else {
                            net_log("ICE-SIGNAL", "ERROR: juice_set_remote_description failed with code %d", res);
                        }
                    }
                    LeaveCriticalSection(&g_ice.cs);
                }
                // Check if message is remote candidate
                else if (strstr(r_topic, "/cand") != NULL && strstr(r_topic, "/done") == NULL) {
                    EnterCriticalSection(&g_ice.cs);
                    if (strlen(r_payload) > 0) {
                        // Completely disable LAN connections: drop remote host candidates
                        if (strstr(r_payload, "typ host") != NULL) {
                            net_log("ICE-SIGNAL", "--> [LAN DISABLED] Ignored remote host candidate to enforce WAN hole punching: '%s'", r_payload);
                            LeaveCriticalSection(&g_ice.cs);
                            continue;
                        }

                        if (g_ice.remote_sdp_set) {
                            net_log("ICE-SIGNAL", "Adding remote candidate: '%s'", r_payload);
                            int res = juice_add_remote_candidate(g_ice.agent, r_payload);
                            if (res != JUICE_ERR_SUCCESS) {
                                net_log("ICE-SIGNAL", "juice_add_remote_candidate returned %d", res);
                            }
                        } else {
                            if (g_ice.queued_candidate_count < 32) {
                                strncpy_s(g_ice.queued_candidates[g_ice.queued_candidate_count],
                                          sizeof(g_ice.queued_candidates[0]), r_payload, _TRUNCATE);
                                g_ice.queued_candidate_count++;
                                net_log("ICE-SIGNAL", "Queued remote candidate #%d (waiting for remote SDP): '%s'",
                                        g_ice.queued_candidate_count - 1, r_payload);
                            }
                        }
                    }
                    LeaveCriticalSection(&g_ice.cs);
                }
                // Check if message is remote gathering done
                else if (strstr(r_topic, "/done") != NULL) {
                    net_log("ICE-SIGNAL", "Remote gathering DONE received (ignoring lockout to allow trickle).");
                }
            }
        }

        // Keep-alive ping every 10 seconds
        uint64_t now_us = telemetry_now_us();
        if (now_us - last_ping_us >= 10000000) {
            mqtt_send_ping(g_ice.mqtt_sock);
            last_ping_us = now_us;
        }
    }

    if (g_ice.mqtt_sock != INVALID_SOCKET) {
        closesocket(g_ice.mqtt_sock);
        g_ice.mqtt_sock = INVALID_SOCKET;
    }

    net_log("ICE-SIGNAL", "Signaling thread terminated.");
    return 0;
}

static bool ice_session_start(bool is_host, uint16_t room_id, const char *token, int port) {
    net_log("ICE", "Starting ICE session (is_host=%d, room_id=%u, token='%s', port=%d)...",
            is_host ? 1 : 0, room_id, token ? token : "none", port);

    memset(&g_ice, 0, sizeof(g_ice));
    InitializeCriticalSection(&g_ice.cs);
    g_ice.is_host = is_host;
    g_ice.room_id = room_id;
    g_ice.mqtt_sock = INVALID_SOCKET;
    if (token && token[0] != '\0') {
        strncpy_s(g_ice.session_token, sizeof(g_ice.session_token), token, _TRUNCATE);
    } else {
        snprintf(g_ice.session_token, sizeof(g_ice.session_token), "room_%u", room_id);
    }

    g_ice.my_sender_id = (uint32_t)GetCurrentProcessId() ^ (uint32_t)telemetry_now_us();
    if (g_ice.my_sender_id == 0) g_ice.my_sender_id = 1;

    juice_set_log_level(JUICE_LOG_LEVEL_DEBUG);
    juice_set_log_handler(on_juice_log);

    juice_config_t config;
    memset(&config, 0, sizeof(config));
    config.concurrency_mode = JUICE_CONCURRENCY_MODE_THREAD;

    if (port > 0 && port <= 65535) {
        config.local_port_range_begin = (uint16_t)port;
        config.local_port_range_end = (uint16_t)(port + 10);
    }

    config.stun_server_host = "stun.l.google.com";
    config.stun_server_port = 19302;

    config.cb_state_changed = on_juice_state_changed;
    config.cb_candidate = on_juice_candidate;
    config.cb_gathering_done = on_juice_gathering_done;
    config.cb_recv = on_juice_recv;
    config.user_ptr = NULL;

    g_ice.agent = juice_create(&config);
    if (!g_ice.agent) {
        net_log("ICE", "CRITICAL: juice_create failed!");
        DeleteCriticalSection(&g_ice.cs);
        return false;
    }

    if (juice_get_local_description(g_ice.agent, g_ice.local_sdp, sizeof(g_ice.local_sdp)) == JUICE_ERR_SUCCESS) {
        g_ice.local_sdp_ready = true;
        net_log("ICE", "Local SDP generated successfully (%d bytes)", (int)strlen(g_ice.local_sdp));
    } else {
        net_log("ICE", "CRITICAL: juice_get_local_description failed!");
        juice_destroy(g_ice.agent);
        g_ice.agent = NULL;
        DeleteCriticalSection(&g_ice.cs);
        return false;
    }

    int g_res = juice_gather_candidates(g_ice.agent);
    net_log("ICE", "juice_gather_candidates initiated (res=%d)", g_res);

    g_ice.is_running = true;
    g_ice.hSignalingThread = CreateThread(NULL, 0, ice_signaling_thread, NULL, 0, NULL);
    if (!g_ice.hSignalingThread) {
        net_log("ICE", "CRITICAL: Failed to create signaling thread!");
        g_ice.is_running = false;
        juice_destroy(g_ice.agent);
        g_ice.agent = NULL;
        DeleteCriticalSection(&g_ice.cs);
        return false;
    }

    return true;
}

static void ice_session_stop(void) {
    if (!g_ice.is_running && !g_ice.agent) return;
    net_log("ICE", "Stopping ICE session...");

    g_ice.is_running = false;
    g_ice.is_connected = false;

    if (g_ice.mqtt_sock != INVALID_SOCKET) {
        closesocket(g_ice.mqtt_sock);
        g_ice.mqtt_sock = INVALID_SOCKET;
    }

    if (g_ice.hSignalingThread) {
        WaitForSingleObject(g_ice.hSignalingThread, 1500);
        CloseHandle(g_ice.hSignalingThread);
        g_ice.hSignalingThread = NULL;
    }

    if (g_ice.agent) {
        juice_destroy(g_ice.agent);
        g_ice.agent = NULL;
    }

    DeleteCriticalSection(&g_ice.cs);
    net_log("ICE", "ICE session stopped cleanly.");
}

// Hook called by WASAPI capture thread to transmit encrypted audio frames
static void network_send_audio_frame(uint32_t seq, const uint8_t *pcm, uint32_t bytes) {
    if (!g_ice.is_running || !g_ice.agent || !pcm || bytes == 0) return;
    if (g_ice.state != JUICE_STATE_CONNECTED && g_ice.state != JUICE_STATE_COMPLETED) return;

    WirePacket packet;
    memset(&packet, 0, sizeof(packet));
    packet.header.magic = PACKET_MAGIC;
    packet.header.packet_type = PKT_TYPE_AUDIO;
    packet.header.sequence = seq;
    packet.header.timestamp_us = telemetry_now_us();
    packet.header.payload_bytes = (uint16_t)bytes;
    packet.header.room_id = g_ice.room_id;
    packet.header.sender_id = g_ice.my_sender_id;

    uint64_t t0 = telemetry_now_us();
    bool enc_ok = crypto_encrypt_payload(&packet.header,
                                         pcm,
                                         bytes,
                                         packet.ciphertext,
                                         packet.nonce,
                                         packet.tag);
    uint64_t t1 = telemetry_now_us();
    telemetry_record_crypto_overhead((double)(t1 - t0));

    if (!enc_ok) return;

    int sent = juice_send(g_ice.agent, (const char*)&packet, sizeof(WirePacket));
    if (sent > 0) {
        telemetry_record_send((size_t)sent);
    }
}


static DWORD WINAPI wasapi_capture_thread(LPVOID param) {
    (void)param;
    DWORD taskIndex = 0;
    HANDLE hTask = AvSetMmThreadCharacteristicsA("Pro Audio", &taskIndex);

    BYTE temp_frame[VOICE_FRAME_BYTES];
    UINT32 accumulated_bytes = 0;

    while (g_audio.is_running) {
        DWORD waitResult = WaitForSingleObject(g_audio.hCaptureEvent, 50);
        if (!g_audio.is_running) break;
        if (waitResult != WAIT_OBJECT_0) continue;

        UINT32 packetLength = 0;
        HRESULT hr = g_audio.pCaptureClient->lpVtbl->GetNextPacketSize(g_audio.pCaptureClient, &packetLength);
        while (SUCCEEDED(hr) && packetLength > 0) {
            BYTE *pData = NULL;
            UINT32 numFramesRead = 0;
            DWORD flags = 0;

            hr = g_audio.pCaptureClient->lpVtbl->GetBuffer(g_audio.pCaptureClient, &pData, &numFramesRead, &flags, NULL, NULL);
            if (SUCCEEDED(hr) && pData) {
                UINT32 bytesAvailable = numFramesRead * g_audio.wfx.nBlockAlign;

                // If silent flag set, treat buffer as zeroes
                if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                    pData = NULL;
                }

                // Chunk incoming samples into exact 10ms (320 bytes) voice frames
                UINT32 src_offset = 0;
                while (src_offset < bytesAvailable) {
                    UINT32 needed = VOICE_FRAME_BYTES - accumulated_bytes;
                    UINT32 to_copy = (bytesAvailable - src_offset < needed) ? (bytesAvailable - src_offset) : needed;

                    if (pData) {
                        memcpy(temp_frame + accumulated_bytes, pData + src_offset, to_copy);
                    } else {
                        memset(temp_frame + accumulated_bytes, 0, to_copy);
                    }

                    accumulated_bytes += to_copy;
                    src_offset += to_copy;

                    if (accumulated_bytes >= VOICE_FRAME_BYTES) {
                        uint32_t seq = g_audio.capture_seq++;

                        // Calculate Mic Peak Amplitude level for visual mic indicator
                        int16_t *s16 = (int16_t*)temp_frame;
                        int32_t peak = 0;
                        for (int s = 0; s < VOICE_SAMPLES_PER_FRAME; s++) {
                            int32_t amp = abs((int32_t)s16[s]);
                            if (amp > peak) peak = amp;
                        }
                        telemetry_record_mic_level(peak);

                        if (g_audio.loopback_test_mode) {
                            // Direct loopback into local ring buffer for testing
                            ring_buffer_push(seq, temp_frame, VOICE_FRAME_BYTES);
                        } else {
                            // Transmit frame over UDP network
                            network_send_audio_frame(seq, temp_frame, VOICE_FRAME_BYTES);
                        }
                        accumulated_bytes = 0;
                    }
                }

                g_audio.pCaptureClient->lpVtbl->ReleaseBuffer(g_audio.pCaptureClient, numFramesRead);
            }
            hr = g_audio.pCaptureClient->lpVtbl->GetNextPacketSize(g_audio.pCaptureClient, &packetLength);
        }
    }

    if (hTask) AvRevertMmThreadCharacteristics(hTask);
    return 0;
}

static DWORD WINAPI wasapi_render_thread(LPVOID param) {
    (void)param;
    DWORD taskIndex = 0;
    HANDLE hTask = AvSetMmThreadCharacteristicsA("Pro Audio", &taskIndex);

    BYTE pcm_frame[VOICE_FRAME_BYTES];

    while (g_audio.is_running) {
        DWORD waitResult = WaitForSingleObject(g_audio.hRenderEvent, 50);
        if (!g_audio.is_running) break;
        if (waitResult != WAIT_OBJECT_0) continue;

        UINT32 padding = 0;
        HRESULT hr = g_audio.pRenderAudioClient->lpVtbl->GetCurrentPadding(g_audio.pRenderAudioClient, &padding);
        if (FAILED(hr)) continue;

        UINT32 framesAvailable = g_audio.render_buffer_frames - padding;
        while (framesAvailable >= VOICE_SAMPLES_PER_FRAME) {
            BYTE *pRenderData = NULL;
            hr = g_audio.pRenderClient->lpVtbl->GetBuffer(g_audio.pRenderClient, VOICE_SAMPLES_PER_FRAME, &pRenderData);
            if (SUCCEEDED(hr) && pRenderData) {
                // Pull 10ms frame from ring buffer (outputs silence automatically if underrun)
                bool got_frame = ring_buffer_pop(pcm_frame, VOICE_FRAME_BYTES);
                if (got_frame && g_audio.playback_volume != 100) {
                    float vol = (float)g_audio.playback_volume / 100.0f;
                    int16_t *s = (int16_t*)pcm_frame;
                    for (int i = 0; i < VOICE_SAMPLES_PER_FRAME; i++) {
                        int32_t val = (int32_t)(s[i] * vol);
                        if (val > 32767) val = 32767;
                        else if (val < -32768) val = -32768;
                        s[i] = (int16_t)val;
                    }
                }
                memcpy(pRenderData, pcm_frame, VOICE_FRAME_BYTES);
                g_audio.pRenderClient->lpVtbl->ReleaseBuffer(g_audio.pRenderClient, VOICE_SAMPLES_PER_FRAME, 0);
            }
            framesAvailable -= VOICE_SAMPLES_PER_FRAME;
        }
    }

    if (hTask) AvRevertMmThreadCharacteristics(hTask);
    return 0;
}

static bool audio_engine_init(void) {
    memset(&g_audio, 0, sizeof(g_audio));
    g_audio.playback_volume = 100;

    // Initialize 16 kHz 16-bit Mono PCM format descriptor
    g_audio.wfx.wFormatTag      = WAVE_FORMAT_PCM;
    g_audio.wfx.nChannels       = VOICE_CHANNELS;
    g_audio.wfx.nSamplesPerSec  = VOICE_SAMPLE_RATE;
    g_audio.wfx.wBitsPerSample  = VOICE_BITS_PER_SAMPLE;
    g_audio.wfx.nBlockAlign     = (g_audio.wfx.nChannels * g_audio.wfx.wBitsPerSample) / 8;
    g_audio.wfx.nAvgBytesPerSec = g_audio.wfx.nSamplesPerSec * g_audio.wfx.nBlockAlign;
    g_audio.wfx.cbSize          = 0;

    HRESULT hr = CoCreateInstance(&LOCAL_CLSID_MMDeviceEnumerator,
                                  NULL,
                                  CLSCTX_ALL,
                                  &LOCAL_IID_IMMDeviceEnumerator,
                                  (void**)&g_audio.pEnumerator);
    if (FAILED(hr) || !g_audio.pEnumerator) {
        return false;
    }

    // Enumerate active microphone and speaker endpoints
    audio_enumerate_devices(eCapture, &g_audio.capture_devices);
    audio_enumerate_devices(eRender, &g_audio.render_devices);

    return true;
}

static bool audio_engine_start(int capture_dev_idx, int render_dev_idx, bool loopback_mode) {
    if (!g_audio.pEnumerator) return false;
    ring_buffer_reset();
    g_audio.loopback_test_mode = loopback_mode;
    g_audio.capture_seq = 0;

    HRESULT hr;

    // 1. Activate Capture Device
    if (capture_dev_idx >= 0 && capture_dev_idx < g_audio.capture_devices.count) {
        hr = g_audio.pEnumerator->lpVtbl->GetDevice(g_audio.pEnumerator, g_audio.capture_devices.devices[capture_dev_idx].id, &g_audio.pCaptureDevice);
    } else {
        hr = g_audio.pEnumerator->lpVtbl->GetDefaultAudioEndpoint(g_audio.pEnumerator, eCapture, eConsole, &g_audio.pCaptureDevice);
    }
    if (FAILED(hr) || !g_audio.pCaptureDevice) return false;

    hr = g_audio.pCaptureDevice->lpVtbl->Activate(g_audio.pCaptureDevice, &LOCAL_IID_IAudioClient, CLSCTX_ALL, NULL, (void**)&g_audio.pCaptureAudioClient);
    if (FAILED(hr) || !g_audio.pCaptureAudioClient) return false;

    // 10ms buffer duration in 100ns units = 10 * 10000 = 100000
    REFERENCE_TIME hnsRequested = 200000; // 20ms buffer cushion
    DWORD streamFlags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;

    hr = g_audio.pCaptureAudioClient->lpVtbl->Initialize(g_audio.pCaptureAudioClient,
                                                         AUDCLNT_SHAREMODE_SHARED,
                                                         streamFlags,
                                                         hnsRequested,
                                                         0,
                                                         &g_audio.wfx,
                                                         NULL);
    if (FAILED(hr)) return false;

    g_audio.hCaptureEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    hr = g_audio.pCaptureAudioClient->lpVtbl->SetEventHandle(g_audio.pCaptureAudioClient, g_audio.hCaptureEvent);
    if (FAILED(hr)) return false;

    hr = g_audio.pCaptureAudioClient->lpVtbl->GetService(g_audio.pCaptureAudioClient, &LOCAL_IID_IAudioCaptureClient, (void**)&g_audio.pCaptureClient);
    if (FAILED(hr) || !g_audio.pCaptureClient) return false;

    g_audio.pCaptureAudioClient->lpVtbl->GetBufferSize(g_audio.pCaptureAudioClient, &g_audio.capture_buffer_frames);

    // 2. Activate Render Device
    if (render_dev_idx >= 0 && render_dev_idx < g_audio.render_devices.count) {
        hr = g_audio.pEnumerator->lpVtbl->GetDevice(g_audio.pEnumerator, g_audio.render_devices.devices[render_dev_idx].id, &g_audio.pRenderDevice);
    } else {
        hr = g_audio.pEnumerator->lpVtbl->GetDefaultAudioEndpoint(g_audio.pEnumerator, eRender, eConsole, &g_audio.pRenderDevice);
    }
    if (FAILED(hr) || !g_audio.pRenderDevice) return false;

    hr = g_audio.pRenderDevice->lpVtbl->Activate(g_audio.pRenderDevice, &LOCAL_IID_IAudioClient, CLSCTX_ALL, NULL, (void**)&g_audio.pRenderAudioClient);
    if (FAILED(hr) || !g_audio.pRenderAudioClient) return false;

    hr = g_audio.pRenderAudioClient->lpVtbl->Initialize(g_audio.pRenderAudioClient,
                                                       AUDCLNT_SHAREMODE_SHARED,
                                                       streamFlags,
                                                       hnsRequested,
                                                       0,
                                                       &g_audio.wfx,
                                                       NULL);
    if (FAILED(hr)) return false;

    g_audio.hRenderEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    hr = g_audio.pRenderAudioClient->lpVtbl->SetEventHandle(g_audio.pRenderAudioClient, g_audio.hRenderEvent);
    if (FAILED(hr)) return false;

    hr = g_audio.pRenderAudioClient->lpVtbl->GetService(g_audio.pRenderAudioClient, &LOCAL_IID_IAudioRenderClient, (void**)&g_audio.pRenderClient);
    if (FAILED(hr) || !g_audio.pRenderClient) return false;

    g_audio.pRenderAudioClient->lpVtbl->GetBufferSize(g_audio.pRenderAudioClient, &g_audio.render_buffer_frames);

    // 3. Start audio streams and spawn worker threads
    g_audio.is_running = true;

    g_audio.pCaptureAudioClient->lpVtbl->Start(g_audio.pCaptureAudioClient);
    g_audio.pRenderAudioClient->lpVtbl->Start(g_audio.pRenderAudioClient);

    g_audio.hCaptureThread = CreateThread(NULL, 0, wasapi_capture_thread, NULL, 0, NULL);
    g_audio.hRenderThread  = CreateThread(NULL, 0, wasapi_render_thread, NULL, 0, NULL);

    return true;
}

static void audio_engine_stop(void) {
    if (!g_audio.is_running) return;
    g_audio.is_running = false;

    if (g_audio.hCaptureEvent) SetEvent(g_audio.hCaptureEvent);
    if (g_audio.hRenderEvent)  SetEvent(g_audio.hRenderEvent);

    if (g_audio.hCaptureThread) {
        WaitForSingleObject(g_audio.hCaptureThread, 1000);
        CloseHandle(g_audio.hCaptureThread);
        g_audio.hCaptureThread = NULL;
    }
    if (g_audio.hRenderThread) {
        WaitForSingleObject(g_audio.hRenderThread, 1000);
        CloseHandle(g_audio.hRenderThread);
        g_audio.hRenderThread = NULL;
    }

    if (g_audio.pCaptureAudioClient) g_audio.pCaptureAudioClient->lpVtbl->Stop(g_audio.pCaptureAudioClient);
    if (g_audio.pRenderAudioClient)  g_audio.pRenderAudioClient->lpVtbl->Stop(g_audio.pRenderAudioClient);

    if (g_audio.pCaptureClient) { g_audio.pCaptureClient->lpVtbl->Release(g_audio.pCaptureClient); g_audio.pCaptureClient = NULL; }
    if (g_audio.pRenderClient)  { g_audio.pRenderClient->lpVtbl->Release(g_audio.pRenderClient); g_audio.pRenderClient = NULL; }
    if (g_audio.pCaptureAudioClient) { g_audio.pCaptureAudioClient->lpVtbl->Release(g_audio.pCaptureAudioClient); g_audio.pCaptureAudioClient = NULL; }
    if (g_audio.pRenderAudioClient)  { g_audio.pRenderAudioClient->lpVtbl->Release(g_audio.pRenderAudioClient); g_audio.pRenderAudioClient = NULL; }
    if (g_audio.pCaptureDevice) { g_audio.pCaptureDevice->lpVtbl->Release(g_audio.pCaptureDevice); g_audio.pCaptureDevice = NULL; }
    if (g_audio.pRenderDevice)  { g_audio.pRenderDevice->lpVtbl->Release(g_audio.pRenderDevice); g_audio.pRenderDevice = NULL; }

    if (g_audio.hCaptureEvent) { CloseHandle(g_audio.hCaptureEvent); g_audio.hCaptureEvent = NULL; }
    if (g_audio.hRenderEvent)  { CloseHandle(g_audio.hRenderEvent); g_audio.hRenderEvent = NULL; }
}

static void audio_engine_cleanup(void) {
    audio_engine_stop();
    if (g_audio.pEnumerator) {
        g_audio.pEnumerator->lpVtbl->Release(g_audio.pEnumerator);
        g_audio.pEnumerator = NULL;
    }
}


////////////////////////////////////////////////////////////////////////////////
// WIN32 GUI & TELEMETRY HUD (PHASE 6 & INVITE SYSTEM)
////////////////////////////////////////////////////////////////////////////////
#define IDT_TELEMETRY_TIMER     101

#define IDC_EDIT_INVITE         201
#define IDC_BTN_COPY_INVITE     202
#define IDC_BTN_PASTE_INVITE    203
#define IDC_EDIT_IP             204
#define IDC_EDIT_PORT           205
#define IDC_EDIT_ROOM           206
#define IDC_EDIT_KEY            207
#define IDC_BTN_HOST            208
#define IDC_BTN_JOIN            209
#define IDC_BTN_DISCONNECT      210
#define IDC_COMBO_MIC           211
#define IDC_COMBO_SPK           212
#define IDC_CHK_HUD             213
#define IDC_EDIT_HUD            214
#define IDC_STATIC_STATUS       215
#define IDC_BTN_TEST_MIC        216
#define IDC_SLIDER_PEER_VOL     217
#define IDC_BTN_DARK_MODE       218

typedef struct GuiControls {
    HWND hwndMain;
    HWND hEditInvite;
    HWND hBtnCopyInvite;
    HWND hBtnPasteInvite;
    HWND hEditIp;
    HWND hEditPort;
    HWND hEditRoom;
    HWND hEditKey;
    HWND hBtnHost;
    HWND hBtnJoin;
    HWND hBtnDisconnect;
    HWND hComboMic;
    HWND hComboSpk;
    HWND hBtnTestMic;
    HWND hChkHud;
    HWND hStaticMic;
    HWND hStaticPeers;
    HWND hStaticParticipant;
    HWND hStaticVolLabel;
    HWND hSliderPeerVol;
    HWND hStaticVolVal;
    HWND hEditHud;
    HWND hBtnDarkMode;
    HWND hStaticStatus;
    HBRUSH hbrDarkBg;
    HBRUSH hbrDarkEdit;
    HFONT hFontUi;
    HFONT hFontMono;
    int   peer_volume;          // 0 to 200%, default 100
    bool  is_dark_mode;
    bool  is_in_call;
    bool  is_host;
    bool  is_testing_mic;
    bool  volume_controls_visible;
} GuiControls;

static GuiControls g_gui;

static void gui_update_status(const char *text) {
    if (g_gui.hStaticStatus) {
        SetWindowTextA(g_gui.hStaticStatus, text);
    }
}

static bool copy_to_clipboard(HWND hwnd, const char *text) {
    if (!text || !OpenClipboard(hwnd)) return false;
    EmptyClipboard();
    size_t len = strlen(text) + 1;
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, len);
    if (!hMem) {
        CloseClipboard();
        return false;
    }
    void *ptr = GlobalLock(hMem);
    if (ptr) {
        memcpy(ptr, text, len);
        GlobalUnlock(hMem);
        SetClipboardData(CF_TEXT, hMem);
    } else {
        GlobalFree(hMem);
    }
    CloseClipboard();
    return true;
}

static bool paste_from_clipboard(HWND hwnd, char *out_text, size_t max_len) {
    if (!out_text || max_len == 0 || !OpenClipboard(hwnd)) return false;
    HANDLE hData = GetClipboardData(CF_TEXT);
    if (!hData) {
        CloseClipboard();
        return false;
    }
    const char *pszText = (const char*)GlobalLock(hData);
    if (!pszText) {
        CloseClipboard();
        return false;
    }
    strncpy_s(out_text, max_len, pszText, _TRUNCATE);
    GlobalUnlock(hData);
    CloseClipboard();
    return true;
}

// Static GUIDs for UPnP NAT Traversal
static const GUID LOCAL_CLSID_UPnPNAT = {0xAE1E00AA, 0x3FD5, 0x403C, {0x8A, 0x27, 0x2B, 0xBD, 0xC3, 0x0C, 0xD0, 0xE1}};
static const GUID LOCAL_IID_IUPnPNAT  = {0xB171C812, 0xCC76, 0x485A, {0x94, 0xD8, 0xB6, 0xB3, 0xA2, 0x79, 0x4E, 0x99}};

static bool upnp_map_port(int port, const char *local_ip) {
    net_log("UPNP", "Attempting UPnP NAT port mapping for LAN: %s:%d (UDP)...", local_ip, port);
    IUPnPNAT *pNat = NULL;
    HRESULT hr = CoCreateInstance(&LOCAL_CLSID_UPnPNAT, NULL, CLSCTX_ALL, &LOCAL_IID_IUPnPNAT, (void**)&pNat);
    if (FAILED(hr) || !pNat) {
        net_log("UPNP", "FAIL: CoCreateInstance(CLSID_UPnPNAT) failed (hr=0x%08X). UPnP service not available or disabled.", (unsigned)hr);
        return false;
    }

    IStaticPortMappingCollection *pMappings = NULL;
    hr = pNat->lpVtbl->get_StaticPortMappingCollection(pNat, &pMappings);
    if (FAILED(hr) || !pMappings) {
        net_log("UPNP", "FAIL: get_StaticPortMappingCollection failed (hr=0x%08X). Router may have UPnP IGD disabled.", (unsigned)hr);
        pNat->lpVtbl->Release(pNat);
        return false;
    }

    wchar_t w_ip[64] = {0};
    MultiByteToWideChar(CP_ACP, 0, local_ip, -1, w_ip, 64);

    BSTR bstrProto = SysAllocString(L"UDP");
    BSTR bstrClient = SysAllocString(w_ip);
    BSTR bstrDesc = SysAllocString(L"preAlphaVoiceChat");

    IStaticPortMapping *pMapping = NULL;
    hr = pMappings->lpVtbl->Add(pMappings, (long)port, bstrProto, (long)port, bstrClient, VARIANT_TRUE, bstrDesc, &pMapping);

    SysFreeString(bstrProto);
    SysFreeString(bstrClient);
    SysFreeString(bstrDesc);

    if (pMapping) pMapping->lpVtbl->Release(pMapping);
    pMappings->lpVtbl->Release(pMappings);
    pNat->lpVtbl->Release(pNat);

    if (SUCCEEDED(hr)) {
        net_log("UPNP", "SUCCESS: Port %d UDP mapped on router! External port %d -> %s:%d", port, port, local_ip, port);
        return true;
    } else {
        net_log("UPNP", "FAIL: Add port mapping failed (hr=0x%08X). Router rejected mapping or conflict exists.", (unsigned)hr);
        return false;
    }
}

static void upnp_unmap_port(int port) {
    net_log("UPNP", "Removing UPnP port mapping for port %d (UDP)...", port);
    IUPnPNAT *pNat = NULL;
    HRESULT hr = CoCreateInstance(&LOCAL_CLSID_UPnPNAT, NULL, CLSCTX_ALL, &LOCAL_IID_IUPnPNAT, (void**)&pNat);
    if (FAILED(hr) || !pNat) return;

    IStaticPortMappingCollection *pMappings = NULL;
    hr = pNat->lpVtbl->get_StaticPortMappingCollection(pNat, &pMappings);
    if (SUCCEEDED(hr) && pMappings) {
        BSTR bstrProto = SysAllocString(L"UDP");
        pMappings->lpVtbl->Remove(pMappings, (long)port, bstrProto);
        SysFreeString(bstrProto);
        pMappings->lpVtbl->Release(pMappings);
        net_log("UPNP", "UPnP port mapping for port %d removed cleanly.", port);
    }
    pNat->lpVtbl->Release(pNat);
}


static bool parse_invite_string(const char *invite,
                                char *wan_ip, size_t wan_ip_len,
                                char *lan_ip, size_t lan_ip_len,
                                int *port, uint16_t *room,
                                char *key, size_t key_len,
                                char *token, size_t token_len) {
    if (token && token_len > 0) token[0] = '\0';
    if (!invite) return false;
    char temp[256];
    strncpy_s(temp, sizeof(temp), invite, _TRUNCATE);

    char *p = temp;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    size_t len = strlen(p);
    while (len > 0 && (p[len - 1] == ' ' || p[len - 1] == '\t' || p[len - 1] == '\r' || p[len - 1] == '\n')) {
        p[--len] = '\0';
    }
    if (len == 0) return false;

    char *colon = strchr(p, ':');
    if (!colon) return false;
    *colon = '\0';

    char *plus = strchr(p, '+');
    if (plus) {
        *plus = '\0';
        strncpy_s(wan_ip, wan_ip_len, p, _TRUNCATE);
        if (lan_ip && lan_ip_len > 0) {
            strncpy_s(lan_ip, lan_ip_len, plus + 1, _TRUNCATE);
        }
    } else {
        strncpy_s(wan_ip, wan_ip_len, p, _TRUNCATE);
        if (lan_ip && lan_ip_len > 0) {
            lan_ip[0] = '\0';
        }
    }

    char *hash1 = strchr(colon + 1, '#');
    if (hash1) {
        *hash1 = '\0';
        *port = atoi(colon + 1);
        char *hash2 = strchr(hash1 + 1, '#');
        if (hash2) {
            *hash2 = '\0';
            *room = (uint16_t)atoi(hash1 + 1);
            char *hash3 = strchr(hash2 + 1, '#');
            if (hash3) {
                *hash3 = '\0';
                strncpy_s(key, key_len, hash2 + 1, _TRUNCATE);
                if (token && token_len > 0) {
                    strncpy_s(token, token_len, hash3 + 1, _TRUNCATE);
                }
            } else {
                strncpy_s(key, key_len, hash2 + 1, _TRUNCATE);
            }
        } else {
            *room = (uint16_t)atoi(hash1 + 1);
        }
    } else {
        *port = atoi(colon + 1);
    }
    return true;
}

static const char *get_connection_type_str(void) {
    if (!g_ice.is_connected) return "Negotiating / Disconnected";
    if (strstr(g_ice.selected_local_cand, "typ host") && strstr(g_ice.selected_remote_cand, "typ host")) {
        return "LAN";
    }
    if (strstr(g_ice.selected_local_cand, "typ relay") || strstr(g_ice.selected_remote_cand, "typ relay")) {
        return "Relay (TURN)";
    }
    if (strstr(g_ice.selected_local_cand, "typ srflx") || strstr(g_ice.selected_remote_cand, "typ srflx")) {
        return "WAN (P2P)";
    }
    // Fallback: check remote IP address format
    if (strncmp(g_ice.selected_remote, "192.168.", 8) == 0 ||
        strncmp(g_ice.selected_remote, "10.", 3) == 0 ||
        strncmp(g_ice.selected_remote, "127.", 4) == 0 ||
        strncmp(g_ice.selected_remote, "172.16.", 7) == 0 ||
        strncmp(g_ice.selected_remote, "172.17.", 7) == 0 ||
        strncmp(g_ice.selected_remote, "172.18.", 7) == 0 ||
        strncmp(g_ice.selected_remote, "172.19.", 7) == 0 ||
        strncmp(g_ice.selected_remote, "172.2", 5) == 0 ||
        strncmp(g_ice.selected_remote, "172.3", 5) == 0) {
        return "LAN";
    }
    return "WAN (P2P)";
}

static void gui_update_hud(void) {
    if (!g_gui.hEditHud || !IsWindow(g_gui.hEditHud)) return;

    telemetry_update_bandwidth();

    TelemetryState snap;
    telemetry_get_snapshot(&snap);

    // If connected but address strings are empty, query them
    if (g_ice.is_connected && g_ice.agent && g_ice.selected_local[0] == '\0') {
        EnterCriticalSection(&g_ice.cs);
        juice_get_selected_addresses(g_ice.agent, g_ice.selected_local, sizeof(g_ice.selected_local),
                                     g_ice.selected_remote, sizeof(g_ice.selected_remote));
        juice_get_selected_candidates(g_ice.agent, g_ice.selected_local_cand, sizeof(g_ice.selected_local_cand),
                                      g_ice.selected_remote_cand, sizeof(g_ice.selected_remote_cand));
        LeaveCriticalSection(&g_ice.cs);
    }

    int active_peers = ice_get_active_peers();
    char mic_str[64];
    char peer_str[80];
    char presence_desc[80];
    const char *status_str = "DISCONNECTED";
    const char *mic_activity_desc = "IDLE";
    const char *conn_type = get_connection_type_str();

    if (g_gui.is_testing_mic) {
        if (snap.mic_peak_level > 600) {
            snprintf(mic_str, sizeof(mic_str), "[ ● MIC: LIVE ] (Level: %d)", snap.mic_peak_level);
            mic_activity_desc = "HEARING VOICE (PLAYBACK)";
        } else {
            snprintf(mic_str, sizeof(mic_str), "[ ○ MIC: TEST ] (Silence)");
            mic_activity_desc = "IDLE (Silence)";
        }
        strcpy_s(peer_str, sizeof(peer_str), "[ Loopback Test Mode ]");
        strcpy_s(presence_desc, sizeof(presence_desc), "Local Mic Loopback Test (Self)");
        status_str = "MIC LOOPBACK TEST (LOCAL PLAYBACK)";
    } else if (g_gui.is_in_call) {
        if (snap.mic_peak_level > 600) {
            snprintf(mic_str, sizeof(mic_str), "[ ● MIC: LIVE ] (Level: %d)", snap.mic_peak_level);
            mic_activity_desc = "TRANSMITTING VOICE";
        } else {
            snprintf(mic_str, sizeof(mic_str), "[ ○ MIC: IDLE ] (Silence)");
            mic_activity_desc = "IDLE (Silence)";
        }

        int total_in_room = active_peers + 1;
        if (total_in_room > 1) {
            snprintf(peer_str, sizeof(peer_str), "● Room: %d People [%s]",
                     total_in_room, conn_type);
            snprintf(presence_desc, sizeof(presence_desc), "%d People in room (%s)",
                     total_in_room, conn_type);
        } else {
            snprintf(peer_str, sizeof(peer_str), "● Room: 1 Person");
            snprintf(presence_desc, sizeof(presence_desc), "1 Person in room (Waiting for peer)");
        }
        status_str = g_gui.is_host ? "HOSTING & STREAMING" : "CONNECTED & STREAMING";
    } else {
        strcpy_s(mic_str, sizeof(mic_str), "[ ○ MIC: OFF ]");
        strcpy_s(peer_str, sizeof(peer_str), "Room: Disconnected");
        strcpy_s(presence_desc, sizeof(presence_desc), "0 People (Disconnected)");
        status_str = "DISCONNECTED";
    }

    if (g_gui.hStaticMic) SetWindowTextA(g_gui.hStaticMic, mic_str);
    if (g_gui.hStaticPeers) SetWindowTextA(g_gui.hStaticPeers, peer_str);

    if (g_gui.hStaticParticipant) {
        int peer_count = 0;
        if (g_gui.is_testing_mic) {
            SetWindowTextA(g_gui.hStaticParticipant, "Participant: Self (Mic Loopback Test)");
        } else if (g_gui.is_in_call) {
            uint64_t now_us = telemetry_now_us();
            uint32_t active_id = 0;
            EnterCriticalSection(&g_ice.cs);
            for (int i = 0; i < MAX_ROOM_PEERS; i++) {
                if (g_ice.peers[i].sender_id != 0 && (now_us - g_ice.peers[i].last_seen_us <= 3500000)) {
                    if (active_id == 0) active_id = g_ice.peers[i].sender_id;
                    peer_count++;
                }
            }
            LeaveCriticalSection(&g_ice.cs);

            char part_buf[96];
            if (peer_count == 0) {
                if (g_ice.is_connected) {
                    snprintf(part_buf, sizeof(part_buf), "Participant: Remote [%s]", conn_type);
                    peer_count = 1;
                } else {
                    strcpy_s(part_buf, sizeof(part_buf), "Participant: None (Waiting)");
                }
            } else if (peer_count == 1) {
                snprintf(part_buf, sizeof(part_buf), "Participant: #%04X [%s]", active_id & 0xFFFF, conn_type);
            } else {
                snprintf(part_buf, sizeof(part_buf), "Participants: %d active [%s]", peer_count, conn_type);
            }
            SetWindowTextA(g_gui.hStaticParticipant, part_buf);
        } else {
            SetWindowTextA(g_gui.hStaticParticipant, "Participant: None (Disconnected)");
        }

        // Show volume slider only when an active peer is in the room
        bool should_show_volume = (g_gui.is_in_call && peer_count > 0);
        if (should_show_volume != g_gui.volume_controls_visible) {
            g_gui.volume_controls_visible = should_show_volume;
            int show_cmd = should_show_volume ? SW_SHOW : SW_HIDE;
            if (g_gui.hStaticVolLabel) ShowWindow(g_gui.hStaticVolLabel, show_cmd);
            if (g_gui.hSliderPeerVol) ShowWindow(g_gui.hSliderPeerVol, show_cmd);
            if (g_gui.hStaticVolVal) ShowWindow(g_gui.hStaticVolVal, show_cmd);

            RECT rcBox = { 280, 275, 580, 318 };
            InvalidateRect(g_gui.hwndMain, &rcBox, TRUE);
        }
    }

    // ASCII volume meter bar (0 to 20 bars)
    char mic_bar[21];
    int bars = (snap.mic_peak_level * 20) / 10000;
    if (bars > 20) bars = 20;
    for (int b = 0; b < 20; b++) mic_bar[b] = (b < bars) ? '|' : ' ';
    mic_bar[20] = '\0';

    char ice_diag_section[512] = {0};
    if (g_ice.is_running) {
        const char *state_desc = juice_state_to_string(g_ice.state);
        char selected_desc[256];
        if (g_ice.is_connected && g_ice.selected_local[0] && g_ice.selected_remote[0]) {
            snprintf(selected_desc, sizeof(selected_desc), "%s <-> %s",
                     g_ice.selected_local, g_ice.selected_remote);
        } else {
            strcpy_s(selected_desc, sizeof(selected_desc), "Gathering/Negotiating candidate pairs...");
        }

        snprintf(ice_diag_section, sizeof(ice_diag_section),
            " -------------------------------------------------------\r\n"
            " [ICE NAT TRAVERSAL DIAGNOSTICS (libjuice RFC 8445)]\r\n"
            " Mode:            WAN P2P Enforced (LAN Host Disabled)\r\n"
            " ICE State:       %s\r\n"
            " Connection Type: %s\r\n"
            " Active Path:     %s\r\n"
            " WAN Candidates:  %d local gathered (STUN: Google)\r\n"
            " Signaling (MQTT):%s (Session: %s)\r\n",
            state_desc,
            g_ice.is_connected ? conn_type : "Negotiating...",
            selected_desc,
            g_ice.local_candidate_count,
            (g_ice.mqtt_sock != INVALID_SOCKET) ? "Connected" : "Reconnecting...",
            g_ice.session_token);

        if (g_ice.is_connected) {
            char stat_buf[256];
            snprintf(stat_buf, sizeof(stat_buf), "Connected [%s]: %s | Voice Active",
                     conn_type, selected_desc);
            SetWindowTextA(g_gui.hStaticStatus, stat_buf);
        }
    }

    char hud_text[1600];
    snprintf(hud_text, sizeof(hud_text),
        "================== LIVE TELEMETRY HUD ==================\r\n"
        " Room Status:     %s\r\n"
        " Room Presence:   %s\r\n"
        " Connection:      %s\r\n"
        " Mic Activity:    [%s] %s\r\n"
        " -------------------------------------------------------\r\n"
        " Bandwidth OUT:   %6.2f KB/s   | Bandwidth IN:    %6.2f KB/s\r\n"
        " Round-Trip Time: %6.2f ms     | Jitter (RFC3550):%6.2f ms\r\n"
        " Packets Sent:    %6llu        | Packets Recv:    %6llu\r\n"
        " Packet Loss:     %6.2f %%      | Out-of-Order:    %6llu\r\n"
        " Ring Buffer:     %2d slot (%4.1f ms) [Target Floor: 10-15 ms]\r\n"
        " AES Crypto Time: %6.2f us / frame (Hardware AES-NI)\r\n"
        "%s"
        "========================================================\r\n",
        status_str,
        presence_desc,
        g_ice.is_connected ? conn_type : "Negotiating...",
        mic_bar, mic_activity_desc,
        snap.kb_per_sec_out, snap.kb_per_sec_in,
        snap.last_rtt_ms, snap.jitter_ms,
        (unsigned long long)snap.total_packets_sent, (unsigned long long)snap.total_packets_recv,
        snap.packet_loss_pct, (unsigned long long)snap.out_of_order_count,
        snap.current_buffer_frames, snap.current_buffer_ms,
        snap.crypto_overhead_us,
        ice_diag_section);

    SetWindowTextA(g_gui.hEditHud, hud_text);
}

static void gui_handle_host(void) {
    char port_str[16] = {0};
    char room_str[16] = {0};
    char key[128] = {0};
    char local_ip[64] = {0};

    GetWindowTextA(g_gui.hEditPort, port_str, sizeof(port_str));
    GetWindowTextA(g_gui.hEditRoom, room_str, sizeof(room_str));
    GetWindowTextA(g_gui.hEditKey, key, sizeof(key));

    int port = atoi(port_str);
    if (port <= 0 || port > 65535) port = DEFAULT_RELAY_PORT;

    uint16_t room_id = (uint16_t)atoi(room_str);
    if (room_id == 0) room_id = 1;

    if (strlen(key) == 0) strcpy_s(key, sizeof(key), "voicechat2026");

    // 1. Discover local LAN IP
    get_local_ip(local_ip, sizeof(local_ip));

    // 2. Generate random 8-character session token for zero-config hole punching
    char session_token[32];
    generate_session_token(session_token, sizeof(session_token));

    net_log("GUI", "HOST ROOM clicked: LAN=%s, Port=%d, Room=%u, Key='%s', Token=%s",
            local_ip, port, room_id, key, session_token);

    // 3. Formulate the invite code (IP:Port#Room#Key#Token)
    char invite_str[256];
    snprintf(invite_str, sizeof(invite_str), "%s:%d#%u#%s#%s",
             local_ip, port, room_id, key, session_token);
    SetWindowTextA(g_gui.hEditInvite, invite_str);
    SetWindowTextA(g_gui.hEditIp, local_ip);
    net_log("GUI", "Invite code generated: '%s'", invite_str);

    // Automatically copy invite to clipboard for convenience
    copy_to_clipboard(g_gui.hwndMain, invite_str);

    // 4. Initialize crypto with pre-shared key
    if (!crypto_init(key)) {
        MessageBoxA(g_gui.hwndMain, "Failed to initialize Windows AES-GCM cryptography!", "Error", MB_ICONERROR);
        return;
    }

    gui_update_status("Starting ICE agent (STUN/TURN) & connecting to signaling...");

    // 5. Start libjuice ICE session
    if (!ice_session_start(true, room_id, session_token, port)) {
        crypto_cleanup();
        MessageBoxA(g_gui.hwndMain, "Failed to start libjuice ICE Agent!", "Error", MB_ICONERROR);
        gui_update_status("Failed to start ICE session.");
        return;
    }

    // 6. Start WASAPI Audio Engine
    int mic_idx = (int)SendMessage(g_gui.hComboMic, CB_GETCURSEL, 0, 0);
    int spk_idx = (int)SendMessage(g_gui.hComboSpk, CB_GETCURSEL, 0, 0);

    if (!audio_engine_start(mic_idx, spk_idx, false)) {
        MessageBoxA(g_gui.hwndMain, "Failed to initialize WASAPI Audio endpoints!", "Error", MB_ICONERROR);
        ice_session_stop();
        crypto_cleanup();
        return;
    }

    g_gui.is_in_call = true;
    g_gui.is_host = true;

    EnableWindow(g_gui.hBtnHost, FALSE);
    EnableWindow(g_gui.hBtnJoin, FALSE);
    EnableWindow(g_gui.hBtnPasteInvite, FALSE);
    EnableWindow(g_gui.hBtnTestMic, FALSE);
    EnableWindow(g_gui.hComboMic, FALSE);
    EnableWindow(g_gui.hComboSpk, FALSE);
    EnableWindow(g_gui.hBtnDisconnect, TRUE);
    EnableWindow(g_gui.hEditIp, FALSE);
    EnableWindow(g_gui.hEditPort, FALSE);
    EnableWindow(g_gui.hEditRoom, FALSE);
    EnableWindow(g_gui.hEditKey, FALSE);

    char status_buf[256];
    snprintf(status_buf, sizeof(status_buf),
             "HOSTING Room %u [Token: %s] | Invite copied to clipboard! Waiting for peer to join...",
             room_id, session_token);
    gui_update_status(status_buf);
}

static void gui_handle_join(void) {
    char ip_entry[128] = {0};
    char port_str[16] = {0};
    char room_str[16] = {0};
    char key[128] = {0};

    GetWindowTextA(g_gui.hEditIp, ip_entry, sizeof(ip_entry));
    GetWindowTextA(g_gui.hEditPort, port_str, sizeof(port_str));
    GetWindowTextA(g_gui.hEditRoom, room_str, sizeof(room_str));
    GetWindowTextA(g_gui.hEditKey, key, sizeof(key));

    int port = atoi(port_str);
    if (port <= 0 || port > 65535) port = DEFAULT_RELAY_PORT;

    uint16_t room_id = (uint16_t)atoi(room_str);
    if (room_id == 0) room_id = 1;

    if (strlen(key) == 0) strcpy_s(key, sizeof(key), "voicechat2026");

    char wan_ip[64] = {0};
    char lan_ip[64] = {0};
    char session_token[32] = {0};

    // First check if an invite was entered in hEditInvite
    char invite_in_box[256] = {0};
    GetWindowTextA(g_gui.hEditInvite, invite_in_box, sizeof(invite_in_box));
    int parsed_port = port;
    uint16_t parsed_room = room_id;
    char parsed_key[128] = {0};

    if (strlen(invite_in_box) > 0 &&
        parse_invite_string(invite_in_box, wan_ip, sizeof(wan_ip), lan_ip, sizeof(lan_ip),
                            &parsed_port, &parsed_room, parsed_key, sizeof(parsed_key),
                            session_token, sizeof(session_token))) {
        port = parsed_port;
        room_id = parsed_room;
        if (strlen(parsed_key) > 0) strcpy_s(key, sizeof(key), parsed_key);

        // Synchronize UI edit controls with the parsed invite code
        if (strlen(lan_ip) > 0) {
            char combined_ip[128];
            snprintf(combined_ip, sizeof(combined_ip), "%s (LAN: %s)", wan_ip, lan_ip);
            SetWindowTextA(g_gui.hEditIp, combined_ip);
        } else {
            SetWindowTextA(g_gui.hEditIp, wan_ip);
        }
        char port_buf[16], room_buf[16];
        snprintf(port_buf, sizeof(port_buf), "%d", port);
        snprintf(room_buf, sizeof(room_buf), "%u", room_id);
        SetWindowTextA(g_gui.hEditPort, port_buf);
        SetWindowTextA(g_gui.hEditRoom, room_buf);
        SetWindowTextA(g_gui.hEditKey, key);
    } else {
        char *plus = strchr(ip_entry, '+');
        if (plus) {
            *plus = '\0';
            strncpy_s(wan_ip, sizeof(wan_ip), ip_entry, _TRUNCATE);
            char *p = plus + 1;
            while (*p == ' ' || *p == '(' || strncmp(p, "LAN:", 4) == 0) {
                if (strncmp(p, "LAN:", 4) == 0) p += 4;
                else p++;
            }
            char clean_lan[64] = {0};
            strncpy_s(clean_lan, sizeof(clean_lan), p, _TRUNCATE);
            char *closing = strchr(clean_lan, ')');
            if (closing) *closing = '\0';
            strncpy_s(lan_ip, sizeof(lan_ip), clean_lan, _TRUNCATE);
        } else {
            strncpy_s(wan_ip, sizeof(wan_ip), ip_entry, _TRUNCATE);
        }
    }

    if (strlen(wan_ip) == 0 && strlen(lan_ip) == 0) {
        strcpy_s(wan_ip, sizeof(wan_ip), "127.0.0.1");
    }

    // Format invite string into edit box
    char invite_str[256];
    if (strlen(session_token) > 0) {
        if (strlen(lan_ip) > 0) {
            snprintf(invite_str, sizeof(invite_str), "%s+%s:%d#%u#%s#%s", wan_ip, lan_ip, port, room_id, key, session_token);
        } else {
            snprintf(invite_str, sizeof(invite_str), "%s:%d#%u#%s#%s", wan_ip, port, room_id, key, session_token);
        }
    } else {
        if (strlen(lan_ip) > 0) {
            snprintf(invite_str, sizeof(invite_str), "%s+%s:%d#%u#%s", wan_ip, lan_ip, port, room_id, key);
        } else {
            snprintf(invite_str, sizeof(invite_str), "%s:%d#%u#%s", wan_ip, port, room_id, key);
        }
    }
    SetWindowTextA(g_gui.hEditInvite, invite_str);
    net_log("GUI", "JOIN ROOM clicked: Target WAN=%s:%d, LAN=%s, Room=%u, Token=%s",
            wan_ip, port, (lan_ip[0] != '\0') ? lan_ip : "none", room_id, session_token);

    gui_update_status("Starting ICE agent (STUN/TURN) & connecting to signaling...");

    // 1. Initialize crypto with pre-shared key
    if (!crypto_init(key)) {
        MessageBoxA(g_gui.hwndMain, "Failed to initialize Windows AES-GCM cryptography!", "Error", MB_ICONERROR);
        return;
    }

    // 2. Start libjuice ICE session as peer (controlling = false)
    if (!ice_session_start(false, room_id, session_token, port)) {
        crypto_cleanup();
        MessageBoxA(g_gui.hwndMain, "Failed to start libjuice ICE Agent!", "Error", MB_ICONERROR);
        gui_update_status("Failed to start ICE session.");
        return;
    }

    // 3. Start WASAPI Audio Engine with selected devices
    int mic_idx = (int)SendMessage(g_gui.hComboMic, CB_GETCURSEL, 0, 0);
    int spk_idx = (int)SendMessage(g_gui.hComboSpk, CB_GETCURSEL, 0, 0);

    if (!audio_engine_start(mic_idx, spk_idx, false)) {
        MessageBoxA(g_gui.hwndMain, "Failed to initialize WASAPI Audio endpoints!", "Error", MB_ICONERROR);
        ice_session_stop();
        crypto_cleanup();
        return;
    }

    g_gui.is_in_call = true;
    g_gui.is_host = false;

    EnableWindow(g_gui.hBtnHost, FALSE);
    EnableWindow(g_gui.hBtnJoin, FALSE);
    EnableWindow(g_gui.hBtnPasteInvite, FALSE);
    EnableWindow(g_gui.hBtnTestMic, FALSE);
    EnableWindow(g_gui.hComboMic, FALSE);
    EnableWindow(g_gui.hComboSpk, FALSE);
    EnableWindow(g_gui.hBtnDisconnect, TRUE);
    EnableWindow(g_gui.hEditIp, FALSE);
    EnableWindow(g_gui.hEditPort, FALSE);
    EnableWindow(g_gui.hEditRoom, FALSE);
    EnableWindow(g_gui.hEditKey, FALSE);

    char status_buf[256];
    snprintf(status_buf, sizeof(status_buf),
             "CONNECTING to Room %u [Token: %s] | Exchanging ICE candidates with host...",
             room_id, session_token);
    gui_update_status(status_buf);
}

static void gui_handle_copy_invite(void) {
    char invite_str[256] = {0};
    GetWindowTextA(g_gui.hEditInvite, invite_str, sizeof(invite_str));
    if (strlen(invite_str) == 0) {
        char ip[64] = {0}, port_str[16] = {0}, room_str[16] = {0}, key[128] = {0};
        GetWindowTextA(g_gui.hEditIp, ip, sizeof(ip));
        GetWindowTextA(g_gui.hEditPort, port_str, sizeof(port_str));
        GetWindowTextA(g_gui.hEditRoom, room_str, sizeof(room_str));
        GetWindowTextA(g_gui.hEditKey, key, sizeof(key));
        if (strlen(ip) == 0) get_local_ip(ip, sizeof(ip));
        if (strlen(port_str) == 0) strcpy_s(port_str, sizeof(port_str), "7777");
        if (strlen(room_str) == 0) strcpy_s(room_str, sizeof(room_str), "1");
        if (strlen(key) == 0) strcpy_s(key, sizeof(key), "voicechat2026");
        snprintf(invite_str, sizeof(invite_str), "%s:%s#%s#%s", ip, port_str, room_str, key);
        SetWindowTextA(g_gui.hEditInvite, invite_str);
    }
    if (copy_to_clipboard(g_gui.hwndMain, invite_str)) {
        gui_update_status("Invite copied to clipboard! Send it to your friends.");
    } else {
        gui_update_status("Failed to access clipboard.");
    }
}

static void gui_handle_paste_invite(void) {
    char clip_str[256] = {0};
    if (!paste_from_clipboard(g_gui.hwndMain, clip_str, sizeof(clip_str)) || strlen(clip_str) == 0) {
        gui_update_status("Clipboard is empty or does not contain valid text.");
        return;
    }

    char wan_ip[64] = {0};
    char lan_ip[64] = {0};
    int port = 7777;
    uint16_t room = 1;
    char key[128] = {0};
    char token[32] = {0};

    if (parse_invite_string(clip_str, wan_ip, sizeof(wan_ip), lan_ip, sizeof(lan_ip), &port, &room, key, sizeof(key), token, sizeof(token))) {
        SetWindowTextA(g_gui.hEditInvite, clip_str);
        if (strlen(lan_ip) > 0) {
            char combined[128];
            snprintf(combined, sizeof(combined), "%s (+LAN: %s)", wan_ip, lan_ip);
            SetWindowTextA(g_gui.hEditIp, combined);
        } else {
            SetWindowTextA(g_gui.hEditIp, wan_ip);
        }

        char port_str[16], room_str[16];
        snprintf(port_str, sizeof(port_str), "%d", port);
        snprintf(room_str, sizeof(room_str), "%u", room);
        SetWindowTextA(g_gui.hEditPort, port_str);
        SetWindowTextA(g_gui.hEditRoom, room_str);
        if (strlen(key) > 0) {
            SetWindowTextA(g_gui.hEditKey, key);
        }
        gui_update_status("Invite loaded! Click 'Join Existing Room' to connect.");
    } else {
        gui_update_status("Could not parse invite string. Format: IP:Port#Room#Key");
    }
}

static void gui_handle_test_mic(void) {
    if (g_gui.is_in_call) {
        MessageBoxA(g_gui.hwndMain, "Cannot test mic while in an active room call. Disconnect first.", "Info", MB_ICONINFORMATION);
        return;
    }

    if (g_gui.is_testing_mic) {
        // Stop Mic Test
        audio_engine_stop();
        g_gui.is_testing_mic = false;
        SetWindowTextA(g_gui.hBtnTestMic, "Test Mic\n(Loopback)");
        EnableWindow(g_gui.hBtnHost, TRUE);
        EnableWindow(g_gui.hBtnJoin, TRUE);
        EnableWindow(g_gui.hBtnPasteInvite, TRUE);
        EnableWindow(g_gui.hComboMic, TRUE);
        EnableWindow(g_gui.hComboSpk, TRUE);
        if (g_gui.hStaticMic) SetWindowTextA(g_gui.hStaticMic, "[ ○ MIC: OFF ]");
        if (g_gui.hStaticPeers) SetWindowTextA(g_gui.hStaticPeers, "Room: Disconnected");
        gui_update_status("Mic test stopped. Ready to connect or host.");
    } else {
        // Start Mic Test
        int mic_idx = (int)SendMessage(g_gui.hComboMic, CB_GETCURSEL, 0, 0);
        int spk_idx = (int)SendMessage(g_gui.hComboSpk, CB_GETCURSEL, 0, 0);

        if (!audio_engine_start(mic_idx, spk_idx, true)) {
            MessageBoxA(g_gui.hwndMain, "Failed to initialize WASAPI Audio devices for test!", "Error", MB_ICONERROR);
            return;
        }

        g_gui.is_testing_mic = true;
        SetWindowTextA(g_gui.hBtnTestMic, "Stop Mic\nTest");
        EnableWindow(g_gui.hBtnHost, FALSE);
        EnableWindow(g_gui.hBtnJoin, FALSE);
        EnableWindow(g_gui.hBtnPasteInvite, FALSE);
        EnableWindow(g_gui.hComboMic, FALSE);
        EnableWindow(g_gui.hComboSpk, FALSE);
        gui_update_status("Testing mic playback... Talk into your mic to hear your voice through your speakers/headphones.");
    }
}

static void gui_handle_disconnect(void) {
    if (g_gui.is_testing_mic) {
        gui_handle_test_mic();
        return;
    }
    if (!g_gui.is_in_call) return;

    audio_engine_stop();
    ice_session_stop();
    crypto_cleanup();

    g_gui.is_in_call = false;
    g_gui.is_host = false;

    EnableWindow(g_gui.hBtnHost, TRUE);
    EnableWindow(g_gui.hBtnJoin, TRUE);
    EnableWindow(g_gui.hBtnPasteInvite, TRUE);
    EnableWindow(g_gui.hBtnTestMic, TRUE);
    EnableWindow(g_gui.hComboMic, TRUE);
    EnableWindow(g_gui.hComboSpk, TRUE);
    EnableWindow(g_gui.hBtnDisconnect, FALSE);
    EnableWindow(g_gui.hEditIp, TRUE);
    EnableWindow(g_gui.hEditPort, TRUE);
    EnableWindow(g_gui.hEditRoom, TRUE);
    EnableWindow(g_gui.hEditKey, TRUE);

    g_gui.volume_controls_visible = false;
    if (g_gui.hStaticVolLabel) ShowWindow(g_gui.hStaticVolLabel, SW_HIDE);
    if (g_gui.hSliderPeerVol) ShowWindow(g_gui.hSliderPeerVol, SW_HIDE);
    if (g_gui.hStaticVolVal) ShowWindow(g_gui.hStaticVolVal, SW_HIDE);
    RECT rcVol = { 280, 275, 580, 318 };
    InvalidateRect(g_gui.hwndMain, &rcVol, TRUE);

    if (g_gui.hStaticMic) SetWindowTextA(g_gui.hStaticMic, "[ ○ MIC: OFF ]");
    if (g_gui.hStaticPeers) SetWindowTextA(g_gui.hStaticPeers, "Room: Disconnected");
    if (g_gui.hStaticParticipant) SetWindowTextA(g_gui.hStaticParticipant, "Participant: None (Disconnected)");
    gui_update_status("Disconnected. Ready to connect.");
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            g_gui.hwndMain = hwnd;
            g_gui.peer_volume = 100;
            g_gui.is_dark_mode = false;
            g_gui.hbrDarkBg = CreateSolidBrush(RGB(24, 24, 24));
            g_gui.hbrDarkEdit = CreateSolidBrush(RGB(38, 38, 38));

            // System font for crisp Win32 controls
            g_gui.hFontUi = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            g_gui.hFontMono = CreateFontA(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                          ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                          DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");

            // Group 1: Room & Connection Settings
            CreateWindowA("BUTTON", " Room & Connection Setup ",
                          WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                          15, 10, 570, 155, hwnd, NULL, NULL, NULL);

            // Invite Code row:
            CreateWindowA("STATIC", "Invite Code:", WS_CHILD | WS_VISIBLE, 25, 30, 75, 20, hwnd, NULL, NULL, NULL);
            g_gui.hEditInvite = CreateWindowA("EDIT", "", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                                              105, 28, 290, 22, hwnd, (HMENU)IDC_EDIT_INVITE, NULL, NULL);

            g_gui.hBtnCopyInvite = CreateWindowA("BUTTON", "Copy Invite", WS_CHILD | WS_VISIBLE,
                                                 405, 27, 80, 24, hwnd, (HMENU)IDC_BTN_COPY_INVITE, NULL, NULL);

            g_gui.hBtnPasteInvite = CreateWindowA("BUTTON", "Paste Invite", WS_CHILD | WS_VISIBLE,
                                                  495, 27, 80, 24, hwnd, (HMENU)IDC_BTN_PASTE_INVITE, NULL, NULL);

            // Manual Connection Details row:
            CreateWindowA("STATIC", "Host IP:", WS_CHILD | WS_VISIBLE, 25, 58, 55, 20, hwnd, NULL, NULL, NULL);
            g_gui.hEditIp = CreateWindowA("EDIT", "127.0.0.1", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                                          85, 56, 150, 22, hwnd, (HMENU)IDC_EDIT_IP, NULL, NULL);

            CreateWindowA("STATIC", "Port:", WS_CHILD | WS_VISIBLE, 245, 58, 35, 20, hwnd, NULL, NULL, NULL);
            g_gui.hEditPort = CreateWindowA("EDIT", "7777", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER,
                                            285, 56, 50, 22, hwnd, (HMENU)IDC_EDIT_PORT, NULL, NULL);

            CreateWindowA("STATIC", "Room ID:", WS_CHILD | WS_VISIBLE, 345, 58, 55, 20, hwnd, NULL, NULL, NULL);
            g_gui.hEditRoom = CreateWindowA("EDIT", "1", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER,
                                            405, 56, 45, 22, hwnd, (HMENU)IDC_EDIT_ROOM, NULL, NULL);

            CreateWindowA("STATIC", "Secret Key:", WS_CHILD | WS_VISIBLE, 25, 86, 70, 20, hwnd, NULL, NULL, NULL);
            g_gui.hEditKey = CreateWindowA("EDIT", "voicechat2026", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                                           100, 84, 350, 22, hwnd, (HMENU)IDC_EDIT_KEY, NULL, NULL);

            // Distinct Action Buttons:
            g_gui.hBtnHost = CreateWindowA("BUTTON", "Create && Host Room",
                                           WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                                           25, 116, 175, 34, hwnd, (HMENU)IDC_BTN_HOST, NULL, NULL);

            g_gui.hBtnJoin = CreateWindowA("BUTTON", "Join Existing Room",
                                           WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                           210, 116, 175, 34, hwnd, (HMENU)IDC_BTN_JOIN, NULL, NULL);

            g_gui.hBtnDisconnect = CreateWindowA("BUTTON", "Disconnect",
                                                 WS_CHILD | WS_VISIBLE | WS_DISABLED,
                                                 395, 116, 180, 34, hwnd, (HMENU)IDC_BTN_DISCONNECT, NULL, NULL);

            // Group 2: Audio Hardware
            CreateWindowA("BUTTON", " Audio Hardware (WASAPI Event-Driven) ",
                          WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                          15, 172, 570, 84, hwnd, NULL, NULL, NULL);

            CreateWindowA("STATIC", "Microphone:", WS_CHILD | WS_VISIBLE, 25, 194, 80, 20, hwnd, NULL, NULL, NULL);
            g_gui.hComboMic = CreateWindowA("COMBOBOX", "", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                            110, 191, 325, 150, hwnd, (HMENU)IDC_COMBO_MIC, NULL, NULL);

            CreateWindowA("STATIC", "Speakers:", WS_CHILD | WS_VISIBLE, 25, 224, 80, 20, hwnd, NULL, NULL, NULL);
            g_gui.hComboSpk = CreateWindowA("COMBOBOX", "", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                            110, 221, 325, 150, hwnd, (HMENU)IDC_COMBO_SPK, NULL, NULL);

            g_gui.hBtnTestMic = CreateWindowA("BUTTON", "Test Mic\n(Loopback)",
                                              WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_MULTILINE,
                                              445, 191, 130, 54, hwnd, (HMENU)IDC_BTN_TEST_MIC, NULL, NULL);

            // Populate Audio Device Dropdowns
            for (int i = 0; i < g_audio.capture_devices.count; i++) {
                SendMessageA(g_gui.hComboMic, CB_ADDSTRING, 0, (LPARAM)g_audio.capture_devices.devices[i].name);
            }
            if (g_audio.capture_devices.default_index >= 0) {
                SendMessage(g_gui.hComboMic, CB_SETCURSEL, (WPARAM)g_audio.capture_devices.default_index, 0);
            }

            for (int i = 0; i < g_audio.render_devices.count; i++) {
                SendMessageA(g_gui.hComboSpk, CB_ADDSTRING, 0, (LPARAM)g_audio.render_devices.devices[i].name);
            }
            if (g_audio.render_devices.default_index >= 0) {
                SendMessage(g_gui.hComboSpk, CB_SETCURSEL, (WPARAM)g_audio.render_devices.default_index, 0);
            }

            // Group 3: Participants & Volume Control
            CreateWindowA("BUTTON", " Participants & Volume Control ",
                          WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                          15, 262, 570, 60, hwnd, NULL, NULL, NULL);

            g_gui.hStaticParticipant = CreateWindowA("STATIC", "Participant: None (Disconnected)",
                                                     WS_CHILD | WS_VISIBLE | SS_LEFT,
                                                     25, 286, 250, 20, hwnd, NULL, NULL, NULL);

            g_gui.hStaticVolLabel = CreateWindowA("STATIC", "Volume:", WS_CHILD, 285, 286, 48, 20, hwnd, NULL, NULL, NULL);

            g_gui.hSliderPeerVol = CreateWindowA(TRACKBAR_CLASSA, "PeerVolume",
                                                 WS_CHILD | TBS_HORZ | TBS_AUTOTICKS,
                                                 335, 282, 180, 28, hwnd, (HMENU)IDC_SLIDER_PEER_VOL, NULL, NULL);
            SendMessage(g_gui.hSliderPeerVol, TBM_SETRANGE, TRUE, MAKELONG(0, 200));
            SendMessage(g_gui.hSliderPeerVol, TBM_SETPOS, TRUE, 100);
            SendMessage(g_gui.hSliderPeerVol, TBM_SETTICFREQ, 25, 0);

            g_gui.hStaticVolVal = CreateWindowA("STATIC", "100%",
                                                WS_CHILD | SS_LEFT,
                                                525, 286, 50, 20, hwnd, NULL, NULL, NULL);
            g_gui.volume_controls_visible = false;

            // Live Indicators: Mic Activity & Room Presence
            g_gui.hStaticMic = CreateWindowA("STATIC", "[ ○ MIC: OFF ]",
                                             WS_CHILD | WS_VISIBLE | SS_LEFT,
                                             15, 330, 220, 20, hwnd, NULL, NULL, NULL);

            g_gui.hStaticPeers = CreateWindowA("STATIC", "Room: Disconnected",
                                               WS_CHILD | WS_VISIBLE | SS_LEFT,
                                               240, 330, 225, 20, hwnd, NULL, NULL, NULL);

            g_gui.hChkHud = CreateWindowA("BUTTON", "Live Telemetry",
                                          WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                          475, 330, 110, 20, hwnd, (HMENU)IDC_CHK_HUD, NULL, NULL);
            SendMessage(g_gui.hChkHud, BM_SETCHECK, BST_CHECKED, 0);

            // Live Telemetry Readout Box
            g_gui.hEditHud = CreateWindowA("EDIT", "",
                                           WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_READONLY | WS_VSCROLL,
                                           15, 354, 570, 140, hwnd, (HMENU)IDC_EDIT_HUD, NULL, NULL);
            SendMessage(g_gui.hEditHud, WM_SETFONT, (WPARAM)g_gui.hFontMono, TRUE);

            // Bottom Bar: Dark Mode Toggle (very bottom left) & Status Bar Label
            g_gui.hBtnDarkMode = CreateWindowA("BUTTON", "Dark Mode",
                                               WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                               15, 502, 90, 25, hwnd, (HMENU)IDC_BTN_DARK_MODE, NULL, NULL);

            g_gui.hStaticStatus = CreateWindowA("STATIC", "Ready. Host a room or join with an invite code.",
                                                WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
                                                115, 506, 470, 20, hwnd, (HMENU)IDC_STATIC_STATUS, NULL, NULL);

            // Apply system fonts to all controls
            HWND child = GetWindow(hwnd, GW_CHILD);
            while (child) {
                if (child != g_gui.hEditHud) {
                    SendMessage(child, WM_SETFONT, (WPARAM)g_gui.hFontUi, TRUE);
                }
                child = GetWindow(child, GW_HWNDNEXT);
            }

            // Start 250ms periodic timer for Live Telemetry HUD updates
            SetTimer(hwnd, IDT_TELEMETRY_TIMER, 250, NULL);
            break;
        }

        case WM_HSCROLL: {
            if ((HWND)lParam == g_gui.hSliderPeerVol) {
                int pos = (int)SendMessage(g_gui.hSliderPeerVol, TBM_GETPOS, 0, 0);
                g_gui.peer_volume = pos;
                g_audio.playback_volume = pos;
                char vol_buf[32];
                snprintf(vol_buf, sizeof(vol_buf), "%d%%", pos);
                SetWindowTextA(g_gui.hStaticVolVal, vol_buf);
            }
            break;
        }

        case WM_ERASEBKGND: {
            HDC hdc = (HDC)wParam;
            RECT rc;
            GetClientRect(hwnd, &rc);
            HBRUSH hbr = g_gui.is_dark_mode ? g_gui.hbrDarkBg : (HBRUSH)(COLOR_BTNFACE + 1);
            FillRect(hdc, &rc, hbr);
            return 1;
        }

        case WM_CTLCOLORSTATIC: {
            HDC hdc = (HDC)wParam;
            HWND ctrl = (HWND)lParam;
            if (ctrl == g_gui.hEditHud) {
                if (g_gui.is_dark_mode) {
                    SetTextColor(hdc, RGB(0, 255, 128));
                    SetBkColor(hdc, RGB(24, 24, 24));
                    return (LRESULT)g_gui.hbrDarkBg;
                } else {
                    SetTextColor(hdc, RGB(0, 0, 0));
                    SetBkColor(hdc, RGB(255, 255, 255));
                    return (LRESULT)GetStockObject(WHITE_BRUSH);
                }
            }
            if (g_gui.is_dark_mode) {
                SetTextColor(hdc, RGB(240, 240, 240));
                SetBkColor(hdc, RGB(24, 24, 24));
                return (LRESULT)g_gui.hbrDarkBg;
            } else {
                SetTextColor(hdc, RGB(0, 0, 0));
                SetBkColor(hdc, GetSysColor(COLOR_BTNFACE));
                return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
            }
        }

        case WM_CTLCOLOREDIT: {
            HDC hdc = (HDC)wParam;
            if (g_gui.is_dark_mode) {
                SetTextColor(hdc, RGB(240, 240, 240));
                SetBkColor(hdc, RGB(38, 38, 38));
                return (LRESULT)g_gui.hbrDarkEdit;
            } else {
                SetTextColor(hdc, RGB(0, 0, 0));
                SetBkColor(hdc, RGB(255, 255, 255));
                return (LRESULT)GetStockObject(WHITE_BRUSH);
            }
        }

        case WM_CTLCOLORBTN: {
            HDC hdc = (HDC)wParam;
            if (g_gui.is_dark_mode) {
                SetTextColor(hdc, RGB(240, 240, 240));
                SetBkColor(hdc, RGB(24, 24, 24));
                return (LRESULT)g_gui.hbrDarkBg;
            } else {
                return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
            }
        }

        case WM_TIMER: {
            if (wParam == IDT_TELEMETRY_TIMER) {
                bool show_hud = (SendMessage(g_gui.hChkHud, BM_GETCHECK, 0, 0) == BST_CHECKED);
                ShowWindow(g_gui.hEditHud, show_hud ? SW_SHOW : SW_HIDE);
                if (show_hud) {
                    gui_update_hud();
                }
            }
            break;
        }

        case WM_COMMAND: {
            WORD id = LOWORD(wParam);
            if (id == IDC_BTN_HOST) {
                gui_handle_host();
            } else if (id == IDC_BTN_JOIN) {
                gui_handle_join();
            } else if (id == IDC_BTN_DISCONNECT) {
                gui_handle_disconnect();
            } else if (id == IDC_BTN_COPY_INVITE) {
                gui_handle_copy_invite();
            } else if (id == IDC_BTN_PASTE_INVITE) {
                gui_handle_paste_invite();
            } else if (id == IDC_BTN_TEST_MIC) {
                gui_handle_test_mic();
            } else if (id == IDC_BTN_DARK_MODE) {
                g_gui.is_dark_mode = !g_gui.is_dark_mode;
                SetWindowTextA(g_gui.hBtnDarkMode, g_gui.is_dark_mode ? "Light Mode" : "Dark Mode");
                InvalidateRect(hwnd, NULL, TRUE);
                RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
            }
            break;
        }

        case WM_DESTROY: {
            if (g_gui.is_testing_mic) {
                gui_handle_test_mic();
            }
            gui_handle_disconnect();
            KillTimer(hwnd, IDT_TELEMETRY_TIMER);
            if (g_gui.hFontMono) DeleteObject(g_gui.hFontMono);
            if (g_gui.hbrDarkBg) DeleteObject(g_gui.hbrDarkBg);
            if (g_gui.hbrDarkEdit) DeleteObject(g_gui.hbrDarkEdit);
            PostQuitMessage(0);
            break;
        }

        default:
            return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
    return 0;
}

////////////////////////////////////////////////////////////////////////////////
// MAIN ENTRY POINT
////////////////////////////////////////////////////////////////////////////////
int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Check command-line overrides if passed
    if (lpCmdLine) {
        if (strstr(lpCmdLine, "--no-log") || strstr(lpCmdLine, "-quiet")) {
            g_verbose_net_log = false;
        } else if (strstr(lpCmdLine, "--log") || strstr(lpCmdLine, "-v") || strstr(lpCmdLine, "--debug")) {
            g_verbose_net_log = true;
        }
    }

    // 0. Initialize Network & Diagnostics Logging System
    net_log_init();

    // 1. Initialize COM, Winsock, and Common Controls
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    InitCommonControls();

    // 2. Initialize Telemetry, Ring Buffer, and Audio Engine
    telemetry_init();
    ring_buffer_init();
    audio_engine_init();

    // 2b. Seed C runtime pseudo-random generator with high entropy
    unsigned int rseed = 0;
    if (BCryptGenRandom(NULL, (PUCHAR)&rseed, sizeof(rseed), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0 || rseed == 0) {
        LARGE_INTEGER qpc;
        QueryPerformanceCounter(&qpc);
        rseed = (unsigned int)(qpc.QuadPart ^ ((uint64_t)GetCurrentProcessId() << 16) ^ (uint64_t)GetTickCount());
    }
    srand(rseed);

    // 3. Register Win32 Window Class
    WNDCLASSEXA wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize        = sizeof(WNDCLASSEXA);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hIcon         = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = "preAlphaVoiceChatClass";

    RegisterClassExA(&wc);

    // 4. Create and Show Main Window
    HWND hwnd = CreateWindowExA(WS_EX_APPWINDOW,
                                "preAlphaVoiceChatClass",
                                "preAlphaVoiceChat - Win32 Low-Latency Voice Client",
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                CW_USEDEFAULT, CW_USEDEFAULT,
                                620, 580,
                                NULL, NULL, hInstance, NULL);

    if (!hwnd) {
        MessageBoxA(NULL, "Failed to create application window!", "Fatal Error", MB_ICONERROR);
        net_log_cleanup();
        return 1;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    // 5. Standard Win32 Message Loop
    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    // 6. Cleanup
    audio_engine_cleanup();
    ring_buffer_cleanup();
    WSACleanup();
    CoUninitialize();
    net_log_cleanup();

    return (int)msg.wParam;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    return WinMain(GetModuleHandle(NULL), NULL, GetCommandLineA(), SW_SHOWNORMAL);
}
