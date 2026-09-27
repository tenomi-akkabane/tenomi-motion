/**
 * MotionMLP weight blob contract (NOR-resident).
 *
 * Flash: landmarks/Model/motion_mlp_data.hex @ MOTION_MLP_NOR_ADDR
 * Replaceable independently (28_motion_train_dk). Appli / palm / HL stay.
 */

#ifndef MOTION_MLP_WEIGHTS_H
#define MOTION_MLP_WEIGHTS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MOTION_MLP_WINDOW_FRAMES  8
#define MOTION_MLP_LANDMARKS      21
#define MOTION_MLP_FEAT_DIM       42
#define MOTION_MLP_INPUT_DIM      336
#define MOTION_MLP_HIDDEN0        128
#define MOTION_MLP_HIDDEN1        64
#define MOTION_MLP_NUM_CLASSES    5

/* NOR slot after HL (0x70580000, ~4 MB). Keep in sync with hl_xspi.c. */
#define MOTION_MLP_NOR_ADDR       0x70A00000UL

#define MOTION_MLP_BLOB_MAGIC     0x504C4D4Du /* memory bytes: 'MMLP' */
#define MOTION_MLP_BLOB_VERSION   1u
#define MOTION_MLP_MAX_CLASSES    8
#define MOTION_MLP_NAME_LEN       32
#define MOTION_MLP_HEADER_SIZE    320

typedef struct {
  uint32_t magic;
  uint32_t version;
  uint16_t window_frames;
  uint16_t landmarks;
  uint16_t feat_dim;
  uint16_t input_dim;
  uint16_t hidden0;
  uint16_t hidden1;
  uint16_t num_classes;
  uint16_t name_len;
  uint32_t payload_bytes;
  uint8_t reserved[36];
  char names[MOTION_MLP_MAX_CLASSES][MOTION_MLP_NAME_LEN];
} motion_mlp_blob_header_t;

#ifdef __cplusplus
}
#endif

#endif /* MOTION_MLP_WEIGHTS_H */
