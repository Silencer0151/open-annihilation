// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

/* COB script runtime records. */
#ifndef OA_CORE_COB_H
#define OA_CORE_COB_H

#include "oa/core/types.h"

#define OA_COB_THREAD_COUNT 8
#define OA_COB_STACK_SLOTS 32

OA_CORE_BEGIN

#pragma pack(push, 1)

/* Header of a loaded COB file. The file holds each table as a byte offset
 * from the start of the file; the loaded header refers to the table instead. */
typedef struct CobScriptHeader {
    int32_t version;
    int32_t script_count;
    int32_t piece_count;
    uint8_t code_word_count[0x4]; /* length of the code, in 32-bit words; a 32-bit count */
    int32_t static_var_count;
    int32_t sound_count;
    oa_ref32 script_offsets; /* a file offset until loaded */
    oa_ref32 script_names;   /* a file offset until loaded */
    oa_ref32 piece_names;    /* a file offset until loaded */
    oa_ref32 code;           /* a file offset until loaded */
    oa_ref32 sounds;         /* a file offset until loaded */
} CobScriptHeader;

/* One COB interpreter thread. */
typedef struct CobThread {
    uint32_t state; /* class in top byte, block reason bits */
    uint32_t pc;
    int32_t stack_top;
    int32_t sleep_ticks;
    int32_t wait_piece;
    int32_t wait_axis;
    uint32_t child;
    uint32_t signal_mask;
    oa_ref32 callback;
    uint32_t stack[OA_COB_STACK_SLOTS]; /* operands and locals */
} CobThread;

/* Per-unit COB instance. */
typedef struct CobMachine {
    oa_ref32 type_tag; /* identifies the object's kind; unused here */
    /* ? SLEEP scale, a 32-bit word: a sleep of n ms lasts time_scale * n / 1000 ticks */
    uint8_t time_scale[0x4];
    oa_ref32 script;                    /* CobScriptHeader */
    uint8_t reserved_after_script[0x4]; /* the engine never reads it */
    oa_ref32 static_vars;               /* int32[static_var_count] */
    oa_ref32 piece_states;
    uint8_t reserved_after_piece_states[0x4]; /* the engine never reads it */
    CobThread threads[OA_COB_THREAD_COUNT];
    int32_t active_threads;
    oa_ref32 object; /* animated model; the model refers to the Unit */
} CobMachine;

#pragma pack(pop)

OA_ASSERT_SIZE(CobScriptHeader, 0x2c);
OA_ASSERT_OFFSET(CobScriptHeader, version, 0x0);
OA_ASSERT_OFFSET(CobScriptHeader, script_count, 0x4);
OA_ASSERT_OFFSET(CobScriptHeader, piece_count, 0x8);
OA_ASSERT_OFFSET(CobScriptHeader, code_word_count, 0xc);
OA_ASSERT_OFFSET(CobScriptHeader, static_var_count, 0x10);
OA_ASSERT_OFFSET(CobScriptHeader, sound_count, 0x14);
OA_ASSERT_OFFSET(CobScriptHeader, script_offsets, 0x18);
OA_ASSERT_OFFSET(CobScriptHeader, script_names, 0x1c);
OA_ASSERT_OFFSET(CobScriptHeader, piece_names, 0x20);
OA_ASSERT_OFFSET(CobScriptHeader, code, 0x24);
OA_ASSERT_OFFSET(CobScriptHeader, sounds, 0x28);

OA_ASSERT_SIZE(CobThread, 0xa4);
OA_ASSERT_OFFSET(CobThread, state, 0x0);
OA_ASSERT_OFFSET(CobThread, pc, 0x4);
OA_ASSERT_OFFSET(CobThread, stack_top, 0x8);
OA_ASSERT_OFFSET(CobThread, sleep_ticks, 0xc);
OA_ASSERT_OFFSET(CobThread, wait_piece, 0x10);
OA_ASSERT_OFFSET(CobThread, wait_axis, 0x14);
OA_ASSERT_OFFSET(CobThread, child, 0x18);
OA_ASSERT_OFFSET(CobThread, signal_mask, 0x1c);
OA_ASSERT_OFFSET(CobThread, callback, 0x20);
OA_ASSERT_OFFSET(CobThread, stack, 0x24);

OA_ASSERT_SIZE(CobMachine, 0x544);
OA_ASSERT_OFFSET(CobMachine, type_tag, 0x0);
OA_ASSERT_OFFSET(CobMachine, time_scale, 0x4);
OA_ASSERT_OFFSET(CobMachine, script, 0x8);
OA_ASSERT_OFFSET(CobMachine, reserved_after_script, 0xc);
OA_ASSERT_OFFSET(CobMachine, static_vars, 0x10);
OA_ASSERT_OFFSET(CobMachine, piece_states, 0x14);
OA_ASSERT_OFFSET(CobMachine, reserved_after_piece_states, 0x18);
OA_ASSERT_OFFSET(CobMachine, threads, 0x1c);
OA_ASSERT_OFFSET(CobMachine, active_threads, 0x53c);
OA_ASSERT_OFFSET(CobMachine, object, 0x540);

OA_CORE_END

#endif
