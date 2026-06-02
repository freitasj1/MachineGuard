/**
 * @file storage.h
 * @brief Interface da task de armazenamento
 */

#pragma once

#include "app_context.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Task de gravação de dados
 * @param arg Ponteiro para app_context_t
 */
void task_sd(void *arg);

#ifdef __cplusplus
}
#endif