#ifndef CANTRIPLE_PROTOCOL_H
#define CANTRIPLE_PROTOCOL_H

/*
 * CAN Triple <-> SavvyCAN link protocol ("CTSC"), version 1.
 *
 * This header is the whole contract between SavvyCAN's CANTriple connection and the dedicated
 * SavvyCAN firmware that runs on the CAN Triple V1 and V2. It is plain C so the firmware can use
 * an identical copy - if anything here changes, bump CTSC_PROTOCOL_VERSION and copy it across.
 *
 * Transport
 *   V1: the on-board ST-LINK virtual COM port (USART1 on the MCU), 7,372,800 baud, 8N1, no flow control.
 *   V2: USB full speed CDC-ACM, 0483:5740. The baud rate set on the port is ignored.
 *   Both are a plain byte stream, so the same framing serves both and SavvyCAN treats them alike.
 *
 * Framing
 *   Every packet is COBS encoded and ends with a single 0x00. Before encoding a packet is
 *       [sync] [type] [payload ...] [crc16 hi] [crc16 lo]
 *   sync is CTSC_SYNC. The CRC is CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflection,
 *   no final xor) over sync, type and payload, sent big endian - the same CRC and COBS the CAN Triple
 *   Device Manager protocol uses. A packet whose first byte is 0x55 is a Device Manager packet, which
 *   the SavvyCAN firmware still answers for the firmware update commands so the normal firmware can
 *   be put back. The payload length is implied by the frame length. All multi-byte payload fields
 *   are little endian.
 *
 * Session
 *   1. host sends HELLO, device answers INFO
 *   2. host sends BUS_CONFIG for each bus, device answers each with ACK
 *   3. host sends START, device answers ACK and begins streaming FRAMES packets
 *   4. host sends PING at least every CTSC_PING_INTERVAL_MS, device answers each with STATUS
 *   5. host sends STOP (or simply goes away) to end it
 *   If the device hears no valid packet for CTSC_HOST_TIMEOUT_MS it stops streaming and switches
 *   every bus off, so a crashed or unplugged host never leaves the CAN Triple acking or transmitting
 *   on a vehicle. Termination is left as it was set; it is a wiring choice, not traffic.
 *
 * Bandwidth
 *   Received frames are packed several to a packet. A classic standard id frame with 8 data bytes
 *   costs 16 bytes, an extended one 18, which keeps three saturated 1 Mbit classic buses inside
 *   both the 737 kB/s of the V1's UART and USB full speed. Three busy CAN FD buses with large
 *   payloads can exceed USB full speed; when that happens the device drops frames rather than
 *   stalling and counts them in STATUS.host_drops.
 */

#include <stdint.h>
#include <stddef.h>

#define CTSC_PROTOCOL_VERSION   1

#define CTSC_SYNC               0x53    /* 'S' */
#define CTSC_MANAGER_SYNC       0x55    /* Device Manager packet, see above */
#define CTSC_MAX_PACKET         1024    /* largest decoded packet, sync and crc included */
#define CTSC_FLUSH_BYTES        448     /* device sends a FRAMES packet once it holds this much ... */
#define CTSC_FLUSH_US           1000    /* ... or once its oldest frame is this old */

#define CTSC_V1_BAUD            7372800
#define CTSC_PING_INTERVAL_MS   500
#define CTSC_HOST_TIMEOUT_MS    3000

#define CTSC_NUM_BUSES          3

/* ---------------- packet types, host -> device ---------------- */
#define CTSC_HELLO              0x01    /* u8 host protocol version                          -> INFO */
#define CTSC_BUS_CONFIG         0x02    /* ctsc_bus_config_t                                 -> ACK  */
#define CTSC_START              0x03    /* u8 start flags                                    -> ACK  */
#define CTSC_STOP               0x04    /* no payload                                        -> ACK  */
#define CTSC_TX                 0x05    /* one or more tx records, back to back; no reply    */
#define CTSC_PING               0x06    /* no payload                                        -> STATUS */

/* ---------------- packet types, device -> host ---------------- */
#define CTSC_INFO               0x81    /* ctsc_info_t */
#define CTSC_ACK                0x82    /* ctsc_ack_t */
#define CTSC_FRAMES             0x83    /* one or more rx records, back to back */
#define CTSC_STATUS             0x84    /* ctsc_status_t followed by num_buses ctsc_bus_status_t */
#define CTSC_LOG                0x85    /* free text from the firmware, not terminated */

/* ---------------- ACK status ---------------- */
#define CTSC_OK                 0
#define CTSC_ERR_BAD_BUS        1
#define CTSC_ERR_UNSUPPORTED    2       /* e.g. CAN FD asked of a V1 */
#define CTSC_ERR_BAD_BITRATE    3       /* no bit timing reaches that rate */
#define CTSC_ERR_BAD_PACKET     4
#define CTSC_ERR_BUSY           5

/* ---------------- bus modes ---------------- */
#define CTSC_MODE_OFF           0
#define CTSC_MODE_NORMAL        1
#define CTSC_MODE_LISTEN_ONLY   2
#define CTSC_MODE_LOOPBACK      3       /* internal loopback, for bench testing */

/* ---------------- BUS_CONFIG flags ---------------- */
#define CTSC_CFG_FD             0x01    /* run the bus as CAN FD */
#define CTSC_CFG_BRS            0x02    /* frames we transmit use the data bit rate (FD only) */
#define CTSC_CFG_TERMINATION    0x04    /* switch the 120 ohm terminator in */

/* ---------------- START flags ---------------- */
#define CTSC_START_ECHO_TX      0x01    /* report frames we transmit as records with CTSC_HDR_ECHO */

/* ---------------- INFO capability bits ---------------- */
#define CTSC_CAP_FD             0x0001
#define CTSC_CAP_TERMINATION    0x0002
#define CTSC_CAP_LISTEN_ONLY    0x0004
#define CTSC_CAP_ECHO           0x0008
#define CTSC_CAP_LOOPBACK       0x0010

#define CTSC_MODEL_V1           1
#define CTSC_MODEL_V2           2

/* ---------------- frame record header byte ---------------- */
#define CTSC_HDR_BUS_MASK       0x03
#define CTSC_HDR_EXT            0x04    /* 29 bit id, carried as u32; otherwise 11 bit id as u16 */
#define CTSC_HDR_FDF            0x08    /* CAN FD frame */
#define CTSC_HDR_BRS            0x10    /* bit rate switched */
#define CTSC_HDR_ESI            0x20    /* error state indicator */
#define CTSC_HDR_RTR            0x40    /* remote request, carries no data bytes */
#define CTSC_HDR_ECHO           0x80    /* rx record only: we transmitted this frame */

/*
 * Frame records, all fields little endian, no padding:
 *   rx (device -> host, in FRAMES):  u8 hdr, u8 dlc, u32 timestamp_us, id (u16 or u32), data
 *   tx (host -> device, in TX):      u8 hdr, u8 dlc,                   id (u16 or u32), data
 * dlc is the 4 bit DLC code; the number of data bytes is ctsc_dlc_to_len(dlc), or 0 for RTR.
 * timestamp_us is a free running 32 bit microsecond clock, the same one STATUS and ACK report;
 * it wraps every 71.6 minutes and the host unwraps it.
 */
#define CTSC_RX_RECORD_MIN      8       /* hdr + dlc + ts + std id */
#define CTSC_TX_RECORD_MIN      4       /* hdr + dlc + std id */

#if defined(_MSC_VER)
#pragma pack(push, 1)
#define CTSC_PACKED
#else
#define CTSC_PACKED __attribute__((packed))
#endif

typedef struct CTSC_PACKED {
    uint8_t  bus;               /* 0..2 */
    uint8_t  mode;              /* CTSC_MODE_* */
    uint8_t  flags;             /* CTSC_CFG_* */
    uint8_t  reserved;
    uint32_t nominal_bps;       /* arbitration bit rate */
    uint32_t data_bps;          /* data phase bit rate, ignored unless CTSC_CFG_FD */
} ctsc_bus_config_t;

typedef struct CTSC_PACKED {
    uint8_t  proto_version;
    uint8_t  model;             /* CTSC_MODEL_* */
    uint8_t  hw_revision;       /* e.g. 0 for 2.00, 1 for 2.01 */
    uint8_t  num_buses;
    uint16_t caps;              /* CTSC_CAP_* */
    uint8_t  fw_major;
    uint8_t  fw_minor;
    uint8_t  fw_patch;
    uint8_t  reserved;
    uint32_t can_clock_hz;
    uint32_t max_nominal_bps;
    uint32_t max_data_bps;      /* 0 when the model has no CAN FD */
    char     serial[24];        /* MCU UID as hex, padded with zeros */
} ctsc_info_t;

typedef struct CTSC_PACKED {
    uint8_t  acked_type;        /* packet type this answers */
    uint8_t  status;            /* CTSC_OK or CTSC_ERR_* */
    uint8_t  bus;               /* for BUS_CONFIG, 0xFF otherwise */
    uint8_t  reserved;
    uint32_t time_us;           /* device clock when the command was carried out */
} ctsc_ack_t;

typedef struct CTSC_PACKED {
    uint32_t time_us;
    uint16_t host_drops;        /* frames lost because the host link could not keep up, since last STATUS */
    uint8_t  num_buses;
    uint8_t  reserved;
} ctsc_status_t;

#define CTSC_STATE_OFF          0
#define CTSC_STATE_ACTIVE       1   /* error active */
#define CTSC_STATE_WARNING      2   /* a counter is at 96 or more */
#define CTSC_STATE_PASSIVE      3
#define CTSC_STATE_BUS_OFF      4

#define CTSC_BST_TERMINATION    0x01
#define CTSC_BST_FD             0x02
#define CTSC_BST_LISTEN_ONLY    0x04

typedef struct CTSC_PACKED {
    uint8_t  state;             /* CTSC_STATE_* */
    uint8_t  tec;               /* transmit error counter, saturates at 255 */
    uint8_t  rec;               /* receive error counter, saturates at 255 */
    uint8_t  flags;             /* CTSC_BST_* */
    uint16_t rx_dropped;        /* controller fifo overruns, since last STATUS */
    uint16_t tx_dropped;        /* tx requests refused because the tx fifo was full, since last STATUS */
    uint16_t bus_errors;        /* protocol errors seen on the bus, since last STATUS */
    uint16_t reserved;
} ctsc_bus_status_t;

#if defined(_MSC_VER)
#pragma pack(pop)
#endif
#undef CTSC_PACKED

/* ---------------- helpers shared by both ends ---------------- */

static inline uint8_t ctsc_dlc_to_len(uint8_t dlc)
{
    static const uint8_t lens[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64 };
    return lens[dlc & 0x0F];
}

static inline uint8_t ctsc_len_to_dlc(uint8_t len)
{
    if (len <= 8)  return len;
    if (len <= 12) return 9;
    if (len <= 16) return 10;
    if (len <= 20) return 11;
    if (len <= 24) return 12;
    if (len <= 32) return 13;
    if (len <= 48) return 14;
    return 15;
}

static inline uint16_t ctsc_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++)
    {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++) crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}

/* COBS encode len bytes from in to out, without the trailing 0x00. out needs len + len/254 + 1
 * bytes. Returns the number of bytes written. */
static inline size_t ctsc_cobs_encode(const uint8_t *in, size_t len, uint8_t *out)
{
    size_t read = 0, write = 1, code_idx = 0;
    uint8_t code = 1;
    while (read < len)
    {
        if (in[read] == 0)
        {
            out[code_idx] = code;
            code = 1;
            code_idx = write++;
            read++;
        }
        else
        {
            out[write++] = in[read++];
            if (++code == 0xFF)
            {
                out[code_idx] = code;
                code = 1;
                code_idx = write++;
            }
        }
    }
    out[code_idx] = code;
    return write;
}

/* COBS decode len bytes (no trailing 0x00) into out, which must hold at least len bytes.
 * Returns the decoded length, or 0 if the input is not valid COBS. */
static inline size_t ctsc_cobs_decode(const uint8_t *in, size_t len, uint8_t *out)
{
    size_t read = 0, write = 0;
    while (read < len)
    {
        uint8_t code = in[read];
        if (code == 0 || read + code > len) return 0;
        read++;
        for (uint8_t i = 1; i < code; i++)
        {
            if (read >= len) return 0;
            out[write++] = in[read++];
        }
        if (code != 0xFF && read < len) out[write++] = 0;
    }
    return write;
}

#endif /* CANTRIPLE_PROTOCOL_H */
