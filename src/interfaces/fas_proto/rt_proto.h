/* Gigafloss FAS wire protocol.
 *
 * One header shared by every MCU and by the ground station. Two transports
 * use the same payload schema:
 *
 *     Ground station from RS-422 framed to FMC
 *     FMC from classic CAN 2.0B to EPB / IMC
 *
 * Goals:
 *   - Every command fits in a single classic-CAN frame (max 8 data bytes).
 *   - One 29-bit extended ID encodes both "what" and "who".
 *   - All multibyte fields little-endian, all structs packed, bit-exact
 *     across the host and the MCUs.
 *
 * CAN extended ID layout, MSB to LSB:
 *
 *     [28:23]  6-bit MSG_TYPE       (rt_msg_t)
 *     [22:20]  3-bit BOARD_KIND     (rt_board_kind_t)
 *     [19:17]  3-bit BOARD_ID       (0..7, from EPB ID resistors)
 *     [16:11]  6-bit CHANNEL        (PWM channel, sensor index, etc)
 *     [10: 8]  3-bit FLAGS          (bit0=NACK, bit1=URGENT, bit2=RSVD)
 *     [ 7: 0]  8-bit SEQ            (per-source counter, wraps)
 */
#ifndef RT_PROTO_H
#define RT_PROTO_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RT_PROTO_VERSION_MAJOR 0
#define RT_PROTO_VERSION_MINOR 3

/* Board kinds. */
typedef enum {
    RT_BOARD_GS  = 0,
    RT_BOARD_FMC = 1,
    RT_BOARD_EPB = 2,
    RT_BOARD_IMC = 3,
    RT_BOARD_RAB = 4,
    RT_BOARD_PMB = 5,
} rt_board_kind_t;

/* Message types. Hex literals are stable wire codes; do not renumber. */
typedef enum {
    /* Liveness and discovery */
    RT_MSG_HEARTBEAT           = 0x01,  /* every board, ~1 Hz */
    RT_MSG_DISCOVERY_REQ       = 0x02,  /* GS to broadcast, "announce yourself" */
    RT_MSG_DISCOVERY_ANNOUNCE  = 0x03,  /* per-board response with rt_announce_t */
    RT_MSG_TIME_SYNC           = 0x04,  /* FMC to broadcast, rt_time_sync_t (~1 Hz) */

    /* Actuator commands (FMC to EPB) */
    RT_MSG_PWM_SET             = 0x10,  /* rt_pwm_set_t */
    RT_MSG_LOAD_SW_SET         = 0x11,  /* rt_load_sw_set_t */
    RT_MSG_ACTUATOR_QUERY      = 0x12,  /* GS asks board for actuator config */
    RT_MSG_ACTUATOR_CONFIG     = 0x13,  /* EPB returns rt_actuator_config_t */
    RT_MSG_ACTUATOR_STATE      = 0x14,  /* EPB echoes current state */
    RT_MSG_ACTUATOR_FAILSAFE   = 0x1F,  /* drop all PWM to safe, load switches off */

    /* Sensor telemetry (EPB to FMC, forwarded to GS) */
    RT_MSG_ADC_BURST           = 0x20,  /* rt_adc_burst_t: 4 packed int16 samples (legacy) */
    RT_MSG_SENSOR_STATUS       = 0x21,  /* per-channel connect / saturation flags */
    RT_MSG_BOARD_STATUS        = 0x22,  /* vmon / isense / temp / fault bits */
    RT_MSG_ADC_SAMPLE          = 0x23,  /* rt_adc_sample_t: one stamped 2-channel sample */

    /* FMC onboard sensor telemetry (FMC to GS over RS-422; board_kind=FMC, id=0).
     * Each payload is <= 8 bytes so it rides one frame. The wire value is a
     * compact integer; the host applies the per-message scale noted below.
     * (Codes stay <= 0x3F because MSG_TYPE is a 6-bit field in the CAN ID.) */
    RT_MSG_FMC_IMU_ACCEL       = 0x24,  /* rt_fmc_vec3_t: LSM6DSOX accel, raw int16.  g  = raw * 0.000061 */
    RT_MSG_FMC_IMU_GYRO        = 0x25,  /* rt_fmc_vec3_t: LSM6DSOX gyro,  raw int16.  dps= raw * 0.00875  */
    RT_MSG_FMC_ACCEL_HG        = 0x26,  /* rt_fmc_vec3_t: ADXL375 high-g, centi-g.    g  = raw * 0.01     */
    RT_MSG_FMC_MAG             = 0x27,  /* rt_fmc_vec3_t: MMC5983MA, milligauss.       uT = raw * 0.1      */
    RT_MSG_FMC_BARO            = 0x28,  /* rt_fmc_baro_t: MS5611 pressure + temperature */
    RT_MSG_FMC_GPS_POS         = 0x29,  /* rt_fmc_gps_pos_t: lat/lon */
    RT_MSG_FMC_GPS_INFO        = 0x2A,  /* rt_fmc_gps_info_t: fix/sats/alt/hdop/speed */
    RT_MSG_FMC_HEALTH          = 0x2B,  /* rt_fmc_health_t: per-sensor presence/ID beacon (~1 Hz) */

    /* PMB (power management board) telemetry, PMB to CAN to FMC bridge to GS,
     * ~10 Hz, each value the mean of the previous 100 ms of oversampled ADC. */
    RT_MSG_PMB_PWR             = 0x2C,  /* rt_pmb_pwr_t:  regulated 8V4/24V0 rail V+I */
    RT_MSG_PMB_VMON            = 0x2D,  /* rt_pmb_vmon_t: VMAIN/VBATT/VGSE + status flags */
    RT_MSG_PMB_TEMP            = 0x2E,  /* rt_pmb_temp_t: ambient/buck-FET/boost-FET temps */
    RT_MSG_FMC_TEMP            = 0x2F,  /* rt_fmc_temp_t: FMC board NTC temperatures */

    /* Ignition (IMC) and recovery (RAB).
     *
     * The IMC (igniter / motor controller) is wired to ONE EPB through three
     * interface pins (arm, disarm, status). The GS picks which EPB that is in
     * its config and addresses these commands to that EPB by board_id; the EPB
     * pulses the matching line and echoes RT_MSG_IMC_STATUS back. */
    RT_MSG_IGN_ARM             = 0x30,  /* GS->EPB: pulse IMC arm line (rt_imc_cmd_t) */
    RT_MSG_IGN_FIRE            = 0x31,
    RT_MSG_IGN_DISARM          = 0x32,  /* GS->EPB: pulse IMC disarm line (rt_imc_cmd_t) */
    RT_MSG_IMC_STATUS          = 0x33,  /* EPB->GS: rt_imc_status_t (~2 Hz + on change) */

    /* FMC peripheral status (FMC to GS over RS-422) and the buzzer-melody
     * downlink (GS to FMC). All codes stay <= 0x3F (MSG_TYPE is 6 bits). */
    RT_MSG_FMC_SD_STATUS       = 0x34,  /* rt_fmc_sd_status_t: SD-card logger state */
    RT_MSG_FMC_RADIO_STATUS    = 0x35,  /* rt_fmc_radio_status_t: RFD900x mirror state */
    RT_MSG_FMC_BUZZER          = 0x36,  /* rt_fmc_buzzer_t: GS->FMC melody note stream */

    /* PMB battery charger (LTC4162). Telemetry PMB to GS; the enable command is
     * GS to PMB (charging is DEFAULT-OFF and only started from the GUI). */
    RT_MSG_PMB_CHARGER         = 0x39,  /* rt_pmb_charger_t: charge current + state */
    RT_MSG_PMB_CHG_EN          = 0x3A,  /* GS->PMB: rt_pmb_chg_en_t (allow/suspend) */

    RT_MSG_RECOVERY_ARM        = 0x37,
    RT_MSG_RECOVERY_DEPLOY     = 0x38,

    /* Debug and control */
    RT_MSG_DEBUG_LOG           = 0x3E,  /* ASCII (zero-padded) log line */
    RT_MSG_ACK                 = 0x3F,  /* generic ack/nack */
} rt_msg_t;

/* Flag bits on the CAN ID. */
#define RT_FLAG_NACK    (1u << 0)
#define RT_FLAG_URGENT  (1u << 1)

/* Pack a CAN extended ID. */
static inline uint32_t rt_can_id_pack(rt_msg_t msg,
                                      rt_board_kind_t kind,
                                      uint8_t board_id,
                                      uint8_t channel,
                                      uint8_t flags,
                                      uint8_t seq)
{
    return ((uint32_t)(msg      & 0x3F) << 23)
         | ((uint32_t)(kind     & 0x07) << 20)
         | ((uint32_t)(board_id & 0x07) << 17)
         | ((uint32_t)(channel  & 0x3F) << 11)
         | ((uint32_t)(flags    & 0x07) <<  8)
         | ((uint32_t)(seq      & 0xFF));
}

typedef struct {
    uint8_t msg;
    uint8_t kind;
    uint8_t board_id;
    uint8_t channel;
    uint8_t flags;
    uint8_t seq;
} rt_can_id_t;

static inline rt_can_id_t rt_can_id_unpack(uint32_t id)
{
    rt_can_id_t out;
    out.msg      = (uint8_t)((id >> 23) & 0x3F);
    out.kind     = (uint8_t)((id >> 20) & 0x07);
    out.board_id = (uint8_t)((id >> 17) & 0x07);
    out.channel  = (uint8_t)((id >> 11) & 0x3F);
    out.flags    = (uint8_t)((id >>  8) & 0x07);
    out.seq      = (uint8_t)( id        & 0xFF);
    return out;
}

/* Channel capability bits used in rt_actuator_config_t.caps. */
#define RT_CAP_PWM        (1u << 0)   /* channel can drive PWM */
#define RT_CAP_LOAD_SW    (1u << 1)   /* channel has a load switch */
#define RT_CAP_CURRENT    (1u << 2)   /* channel reports sensed current back */

/* Heartbeat payload. Sent ~1 Hz by every board. */
typedef struct __attribute__((packed)) {
    uint32_t uptime_ms;
    uint16_t fw_version;     /* (major << 8) | minor */
    uint8_t  board_id;       /* 0..7, from ID resistors (FMC = 0) */
    uint8_t  flags;          /* bit0 fault, bit1 safe-mode */
} rt_heartbeat_t;

/* Discovery announce: short identity packet so the GS can populate the
 * "what's online" panel without having to query every board.
 */
typedef struct __attribute__((packed)) {
    uint8_t  board_kind;     /* rt_board_kind_t */
    uint8_t  board_id;       /* 0..7 */
    uint8_t  num_channels;   /* number of actuator channels exposed */
    uint8_t  num_sensors;    /* number of analog channels exposed */
    uint16_t fw_version;
    uint8_t  caps_mask;      /* board-level capability summary */
    uint8_t  reserved;
} rt_announce_t;

/* PWM set on one channel. duty_q15 is a 15-bit unsigned fraction of
 * period_us. period_us = 0 means "leave the period alone".
 */
typedef struct __attribute__((packed)) {
    uint16_t duty_q15;
    uint16_t period_us;
    uint32_t reserved;
} rt_pwm_set_t;

/* Load switch set. enable = 0 or 1. hold_ms = 0 means latch on/off,
 * non-zero schedules an automatic switch-off after hold_ms.
 */
typedef struct __attribute__((packed)) {
    uint8_t  enable;
    uint8_t  reserved8;
    uint16_t hold_ms;
    uint32_t reserved32;
} rt_load_sw_set_t;

/* Per-channel actuator config reported by the board. The name is a short
 * fixed-length label so it fits in one CAN frame. The GS keeps a richer
 * name lookup of its own; this is a fallback so power-on works without it.
 */
typedef struct __attribute__((packed)) {
    uint8_t  channel_idx;    /* 0..n-1 */
    uint8_t  caps;           /* RT_CAP_* bits */
    uint8_t  safe_state;     /* 0 = off, 1 = on at safe pulse */
    uint8_t  reserved;
    char     short_name[4];  /* zero-padded ASCII, no terminator required */
} rt_actuator_config_t;

/* Actuator state echoed periodically and on change. */
typedef struct __attribute__((packed)) {
    uint8_t  channel_idx;
    uint8_t  load_sw_on;     /* 0 or 1 */
    uint16_t pulse_us;       /* current commanded pulse width */
    uint16_t period_us;
    uint8_t  fault_bits;
    uint8_t  reserved;
} rt_actuator_state_t;

/* Packed ADC sample: 4 channels of int16 derived from the 24-bit raw by
 * arithmetic shift right by 8. Scaling on the host is described in
 * doc/comments next to the ADS131 driver. (Legacy, untimestamped.)
 */
typedef struct __attribute__((packed)) {
    int16_t ch[4];
} rt_adc_burst_t;

/* Global time sync. The FMC is the time master and broadcasts its free-running
 * microsecond clock ~1 Hz. Every board records (local_us at receipt, t_us) and
 * carries the resulting offset so the timestamps it emits are on the FMC's
 * global timeline. t_us is a uint32 of microseconds; it wraps every ~71.6 min,
 * which is fine because all consumers use modular differences over short
 * windows. */
typedef struct __attribute__((packed)) {
    uint32_t t_us;           /* FMC global microsecond clock at send time */
    uint32_t reserved;
} rt_time_sync_t;

/* One fully-timestamped 2-channel ADC sample. Only AIN0/AIN1 are broken out on
 * the EPB, so we send exactly those two, each as the 24-bit code shifted right
 * by 8 (int16). t_us is the sample's acquisition time on the FMC global
 * timeline (see rt_time_sync_t). Sending the timestamp per sample (rather than
 * once per batch) means the host plots each point at its true acquisition time
 * and any real jitter is visible rather than interpolated away. Exactly 8 bytes
 * so it rides in a single classic-CAN frame. */
typedef struct __attribute__((packed)) {
    uint32_t t_us;
    int16_t  ch0;
    int16_t  ch1;
} rt_adc_sample_t;

/* FMC onboard sensors. All emitted by the FMC directly over RS-422.
 *
 * A generic 3-axis vector reused by the inertial/magnetic sensors. The unit of
 * the int16 depends on the RT_MSG_* code carrying it (see the enum comments):
 *   RT_MSG_FMC_IMU_ACCEL  raw LSM6DSOX counts  (g   = v * 0.000061, +/-2 g FS)
 *   RT_MSG_FMC_IMU_GYRO   raw LSM6DSOX counts  (dps = v * 0.00875,  +/-250 dps FS)
 *   RT_MSG_FMC_ACCEL_HG   ADXL375 centi-g      (g   = v * 0.01)
 *   RT_MSG_FMC_MAG        MMC5983MA milligauss (uT  = v * 0.1) */
typedef struct __attribute__((packed)) {
    int16_t  x;
    int16_t  y;
    int16_t  z;
    uint16_t t_ms;           /* FMC global clock low 16 bits, ms (wraps ~65 s).
                              * Lets the host plot each sample at its true
                              * acquisition time instead of its arrival time, so
                              * a bursty link doesn't make the trace look jagged. */
} rt_fmc_vec3_t;

/* MS5611 barometer. Altitude is derived on the host from pressure_pa so the
 * wire stays 6 bytes: alt_m = 44330 * (1 - (pressure_pa / 101325)^0.190295). */
typedef struct __attribute__((packed)) {
    int32_t pressure_pa;     /* absolute pressure, pascals */
    int16_t temp_cc;         /* temperature, centi-degrees C (degC * 100) */
} rt_fmc_baro_t;

/* GPS position. Lat/lon as fixed-point 1e-7 degrees (the u-blox/NMEA convention),
 * which gives ~1.1 cm resolution and fits two int32 in exactly 8 bytes. */
typedef struct __attribute__((packed)) {
    int32_t lat_1e7;         /* latitude  in degrees * 1e7, +N */
    int32_t lon_1e7;         /* longitude in degrees * 1e7, +E */
} rt_fmc_gps_pos_t;

/* GPS fix quality / derived scalars. Sent alongside RT_MSG_FMC_GPS_POS. */
typedef struct __attribute__((packed)) {
    int16_t  alt_m;          /* MSL altitude, metres */
    uint8_t  fix_quality;    /* 0=no fix, 1=GPS, 2=DGPS, 4=RTK fixed, ... */
    uint8_t  sats;           /* satellites used in the solution */
    uint16_t hdop_x10;       /* horizontal dilution of precision * 10 */
    uint16_t speed_cms;      /* ground speed, cm/s */
} rt_fmc_gps_info_t;

/* Sensor-health beacon: lets the GS show which onboard sensors are actually
 * responding. The *_id fields are the raw identity-register reads so a partially
 * populated/faulty board is diagnosable at a glance (expected: IMU 0x6C,
 * ACCEL 0xE5, MAG 0x30; baro_c1 is the MS5611 PROM C1, nonzero when present). */
typedef struct __attribute__((packed)) {
    uint8_t  imu_id;         /* LSM6DSOX WHO_AM_I  */
    uint8_t  accel_id;       /* ADXL375 DEVID      */
    uint8_t  mag_id;         /* MMC5983MA prod ID  */
    uint8_t  present_mask;   /* which sensors are streaming (FMC_SENS_* in firmware) */
    uint16_t baro_c1;        /* MS5611 PROM coefficient C1 */
    uint8_t  gps_fix;        /* current GPS fix quality */
    uint8_t  gps_sats;       /* satellites used */
} rt_fmc_health_t;

/* PMB regulated output rails: the 8V4 buck and 24V0 boost, each as voltage
 * (millivolts, from a 100k/10k = /11 divider) and current (milliamps, from an
 * INA186A2 (gain 50) across a 2 mOhm shunt). All fields are 100 ms means. */
typedef struct __attribute__((packed)) {
    uint16_t v_8v4_mv;       /* 8V4 rail voltage, mV */
    uint16_t i_8v4_ma;       /* 8V4 rail current, mA */
    uint16_t v_24v0_mv;      /* 24V0 rail voltage, mV */
    uint16_t i_24v0_ma;      /* 24V0 rail current, mA */
} rt_pmb_pwr_t;

/* PMB input / battery voltages (mV, /11 dividers) plus status flags. */
#define RT_PMB_FLAG_BUCK_ON      (1u << 0)  /* 8V4 buck enable asserted */
#define RT_PMB_FLAG_BOOST_ON     (1u << 1)  /* 24V0 boost enable asserted */
#define RT_PMB_FLAG_PG_3V3       (1u << 2)  /* hardware power-good: 3V3 rail */
#define RT_PMB_FLAG_PG_8V4       (1u << 3)  /* hardware power-good: 8V4 rail */
#define RT_PMB_FLAG_PG_24V0      (1u << 4)  /* hardware power-good: 24V0 rail */
#define RT_PMB_FLAG_CHARGER      (1u << 5)  /* LTC4162 charger ACKed on I2C */
#define RT_PMB_FLAG_BATT_SRC     (1u << 6)  /* running from battery (VMAIN tracks VBATT) */
typedef struct __attribute__((packed)) {
    uint16_t v_main_mv;      /* VMAIN (selected input rail), mV */
    uint16_t v_batt_mv;      /* battery voltage, mV */
    uint16_t v_gse_mv;       /* ground-support-equipment input, mV */
    uint8_t  flags;          /* RT_PMB_FLAG_* */
    uint8_t  reserved;
} rt_pmb_vmon_t;

/* PMB temperatures from NCU18XH103 NTCs (10k, NTC-on-top / 4.7k-bottom),
 * centi-degrees C. 0x7FFF = open/short/invalid. */
typedef struct __attribute__((packed)) {
    int16_t  temp_amb_cc;        /* ambient */
    int16_t  temp_buck_cc;       /* 8V4 buck FETs */
    int16_t  temp_boost_cc;      /* 24V0 boost FETs */
    uint16_t reserved;
} rt_pmb_temp_t;

/* PMB battery charger (LTC4162-L) telemetry. Charging is DEFAULT-OFF; the GUI
 * must explicitly allow it (RT_MSG_PMB_CHG_EN). i_chg_ma is the measured battery
 * current (IBAT, + = charging into the battery); v_bat_mv is the charger's own
 * battery-voltage telemetry. state/status are compacted LTC4162 charger_state /
 * charge_status codes (one-hot to small index). */
#define RT_CHG_FLAG_PRESENT  (1u << 0)  /* LTC4162 ACKed on I2C */
#define RT_CHG_FLAG_ENABLED  (1u << 1)  /* charging allowed (suspend bit clear) */
#define RT_CHG_FLAG_VIN_GOOD (1u << 2)  /* input (VGSE) sufficient to charge */
#define RT_CHG_FLAG_CHARGING (1u << 3)  /* actively in a charge state (CC/CV/pre) */
typedef struct __attribute__((packed)) {
    int16_t  i_chg_ma;       /* battery charge current (IBAT), mA, signed */
    uint16_t v_bat_mv;       /* battery voltage (charger telemetry), mV */
    uint8_t  flags;          /* RT_CHG_FLAG_* */
    uint8_t  state;          /* compacted charger_state (see gs decode) */
    uint8_t  status;         /* compacted charge_status */
    uint8_t  cells;          /* configured cell count */
} rt_pmb_charger_t;

/* GS to PMB: allow or suspend battery charging, and optionally set the charge
 * current / voltage DACs. enable = 1 clears the LTC4162 suspend_charger bit
 * (charging proceeds when VGSE + a battery are present); enable = 0 sets it.
 * i_setting / v_setting are the LTC4162 5-bit DAC codes (0..31); 0xFF means
 * "leave unchanged". Charge current = (i_setting+1)/32 * 35.6mV/RSNSB; charge
 * voltage/cell (Li-ion) = 3.8125 V + v_setting * 12.5 mV. Default is suspended. */
typedef struct __attribute__((packed)) {
    uint8_t  enable;         /* 0 = suspend, 1 = allow */
    uint8_t  i_setting;      /* CHARGE_CURRENT_SETTING 0..31, 0xFF = leave */
    uint8_t  v_setting;      /* VCHARGE_SETTING 0..31, 0xFF = leave */
    uint8_t  reserved[5];
} rt_pmb_chg_en_t;

/* FMC board temperatures from NCU18XH103 NTCs (10k, NTC-on-top to 3V3 /
 * 5.0k-bottom to GND), centi-degrees C. 0x7FFF = open/short/invalid.
 * temp_h7 is the NTC by the STM32H7; temp_pwr is by the power/regulator area. */
typedef struct __attribute__((packed)) {
    int16_t  temp_h7_cc;
    int16_t  temp_pwr_cc;
    uint32_t reserved;
} rt_fmc_temp_t;

/* FMC SD-card logger status. state = RT_SD_STATE_*; err = last FatFs FRESULT
 * (0 = OK); free_mb ~ free space in MB (capped at 65535); written_kb = bytes
 * appended to the active log this session, divided by 1024. */
#define RT_SD_STATE_ABSENT    0  /* no card detected */
#define RT_SD_STATE_NO_FS     1  /* card present but no usable filesystem */
#define RT_SD_STATE_MOUNTED   2  /* mounted, ready to log */
#define RT_SD_STATE_LOGGING   3  /* actively appending the CSV log */
#define RT_SD_STATE_ERROR     4  /* fault, see err (FRESULT) */
typedef struct __attribute__((packed)) {
    uint8_t  state;          /* RT_SD_STATE_* */
    uint8_t  err;            /* last FatFs FRESULT, 0 = OK */
    uint16_t free_mb;        /* approximate free space, MB */
    uint32_t written_kb;     /* bytes appended this session / 1024 */
} rt_fmc_sd_status_t;

/* FMC RFD900x telemetry-radio mirror status. The FMC re-transmits selected
 * downlink frames out USART1 to the radio. flags = RT_RFD_FLAG_*; every_n =
 * decimation (1 = mirror every frame, N = 1 of every N); tx_frames/tx_bytes
 * are per-session counters (tx_frames wraps at 16 bits). */
#define RT_RFD_FLAG_POWERED   (1u << 0)  /* load switch (PA15) enabled */
#define RT_RFD_FLAG_ENABLED   (1u << 1)  /* downlink mirroring active */
typedef struct __attribute__((packed)) {
    uint8_t  flags;          /* RT_RFD_FLAG_* */
    uint8_t  every_n;        /* downlink decimation, 1 = every frame */
    uint16_t tx_frames;      /* frames mirrored this session (wraps) */
    uint32_t tx_bytes;       /* bytes mirrored this session */
} rt_fmc_radio_status_t;

/* Buzzer melody command, GS to FMC. The host decodes an uploaded audio clip
 * into a short note list and streams it note-by-note so the firmware needs no
 * audio decoding and almost no memory:
 *   op = RT_BUZZER_OP_BEGIN : clear the melody buffer (other fields ignored)
 *   op = RT_BUZZER_OP_NOTE  : append one note; idx = note number for ordering
 *   op = RT_BUZZER_OP_PLAY  : begin non-blocking playback of the buffer
 *   op = RT_BUZZER_OP_STOP  : stop playback and silence the buzzer
 * freq_hz = 0 in a NOTE is a rest (silent gap of dur_ms). */
#define RT_BUZZER_OP_BEGIN    0
#define RT_BUZZER_OP_NOTE     1
#define RT_BUZZER_OP_PLAY     2
#define RT_BUZZER_OP_STOP     3
typedef struct __attribute__((packed)) {
    uint8_t  op;             /* RT_BUZZER_OP_* */
    uint8_t  idx;            /* note index (ordering / debug) */
    uint16_t freq_hz;        /* tone frequency, Hz (0 = rest) */
    uint16_t dur_ms;         /* note duration, ms */
    uint16_t reserved;
} rt_fmc_buzzer_t;

/* IMC arm/disarm command (GS to the EPB that is wired to the IMC). The EPB
 * drives its arm line on RT_MSG_IGN_ARM and its disarm line on RT_MSG_IGN_DISARM.
 *
 *   pulse_ms : momentary assertion length, milliseconds. 0 = latch the line
 *              until the opposite command (level mode).
 *
 * Exactly 8 bytes so it rides in one classic-CAN frame. */
typedef struct __attribute__((packed)) {
    uint16_t pulse_ms;
    uint8_t  reserved8;
    uint8_t  reserved8b;
    uint32_t reserved32;
} rt_imc_cmd_t;

/* IMC status echoed by the EPB (~2 Hz and on every change). The board_id in the
 * CAN ID says which EPB this is; the GS shows the one it has bound to the IMC.
 *
 *   armed       : IMC STATUS input (PA4). 1 = armed (line at 3.3 V).
 *   arm_line    : current level being driven on the arm output (PA2).
 *   disarm_line : current level being driven on the disarm output (PA3).
 *
 * arm_line / disarm_line let the GS see a pulse that is in flight. */
typedef struct __attribute__((packed)) {
    uint8_t  armed;
    uint8_t  arm_line;
    uint8_t  disarm_line;
    uint8_t  flags;
    uint32_t reserved;
} rt_imc_status_t;

/* Per-channel sensor status reported a few times per second.
 * Each bit set in connected_mask, saturated_mask refers to channel index.
 */
typedef struct __attribute__((packed)) {
    uint8_t  connected_mask;
    uint8_t  saturated_mask;
    uint8_t  error_mask;
    uint8_t  reserved;
    uint32_t reserved32;
} rt_sensor_status_t;

/* Board health telemetry. */
typedef struct __attribute__((packed)) {
    uint16_t vmon_8v4_mv;
    uint16_t vmon_24v_mv;
    uint16_t isense_8v4_ma;
    uint16_t isense_24v_ma;
} rt_board_status_t;

/* Generic ack. nack_code = 0 means OK. */
typedef struct __attribute__((packed)) {
    uint8_t  acked_msg;
    uint8_t  acked_seq;
    uint8_t  nack_code;
    uint8_t  reserved;
    uint32_t reserved32;
} rt_ack_t;

/* RS-422 framing between the GS and the FMC.
 *
 *   [0xAA] [LEN_LO] [LEN_HI] [PAYLOAD ...] [CRC16_LO] [CRC16_HI]
 *
 * PAYLOAD bytes 0..3 = 32-bit CAN extended ID (LE), then 0..8 data bytes.
 * LEN is the count of payload bytes (4 to 12). CRC is CRC-16/CCITT-FALSE
 * over LEN_LO, LEN_HI, and the payload.
 */
#define RT_RS422_MAGIC           0xAA
#define RT_RS422_MAX_PAYLOAD     (4 + 8)
#define RT_RS422_FRAME_MAX_BYTES (1 + 2 + RT_RS422_MAX_PAYLOAD + 2)

uint16_t rt_crc16(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif
#endif /* RT_PROTO_H */
