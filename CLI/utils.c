/* Copyright 2013-present Barefoot Networks, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Antonin Bas (antonin@barefootnetworks.com)
 *
 */

#include "PI/pi.h"

#include <string.h>
#include <stdlib.h>

int count_tokens(const char *str) {
  int count = 0;
  const char *ptr = str;
  while((ptr = strchr(ptr, ' ')) != NULL) {
    count++;
    ptr++;
  }
  return count;
}

// TODO(antonin): make this method robust
char *get_token_from_buffer(char *buffer, size_t index) {
  char *e = buffer;
  for (size_t i = 0; i < index; i++) {
    e = strchr(e, ' ');
    e++;
  }
  e = strdup(e);
  char *c = strchr(e, ' ');
  if (c) *c = '\0';
  return e;
}

extern pi_p4info_t *p4info;

char *complete_p4_table(const char *text, int len, int state) {
  static pi_p4_id_t id;
  if (!state) id = pi_p4info_table_begin(p4info);
  while (id != pi_p4info_table_end(p4info)) {
    const char *name = pi_p4info_table_name_from_id(p4info, id);
    id = pi_p4info_table_next(p4info, id);
    if (!strncmp(name, text, len)) return strdup(name);
  }
  return NULL;
}
#include <stdio.h>
char *complete_p4_action(const char *text, int len, int state,
                         const char *table) {
  static pi_p4_id_t t_id;
  static const pi_p4_id_t *actions;
  static size_t num_actions;
  static size_t index;
  if (!state) {
    t_id = pi_p4info_table_id_from_name(p4info, table);
    if (t_id == PI_INVALID_ID) return NULL;
    actions = pi_p4info_table_get_actions(p4info, t_id, &num_actions);
    index = 0;
  } else if (t_id == PI_INVALID_ID) {
    return NULL;
  }
  assert(actions);

  while (index < num_actions) {
    const char *name = pi_p4info_action_name_from_id(p4info, actions[index]);
    index++;
    if (!strncmp(name, text, len)) return strdup(name);
  }
  return NULL;
}

size_t parse_fixed_args(char *s, const char **dest, size_t expected) {
  for (size_t i = 0; i < expected; i++) {
    dest[i] = (i == 0) ? strtok(s, " ") : strtok(NULL, " ");
    if (!dest[i]) return i;
  }
  return expected;
}

static int try_to_parse_ipv4(char *param, char *bytes) {
  const char *end = strchr(param, '\0');
  char *saveptr;
  const char *b;
  for (int i = 0; i < 4; i++) {
    b = strtok_r(param, ".", &saveptr);
    if (!b || b[0] == '\0') return 1;
    param = NULL;
    char *endptr;
    long int v = strtol(b, &endptr, 10);
    if (*endptr != '\0') return 1;
    if (v < 0 || v >= 256) return 1;
    *bytes++ = (char) v;
  }
  // check that all of the input has been parsed
  if (end != strchr(b, '\0')) return 1;
  return 0;
}

static int try_to_parse_macAddr(char *param, char *bytes) {
  const char *end = strchr(param, '\0');
  char *saveptr;
  const char *b;
  for (int i = 0; i < 6; i++) {
    b = strtok_r(param, ":", &saveptr);
    if (!b || b[0] == '\0') return 1;
    param = NULL;
    char *endptr;
    long int v = strtol(b, &endptr, 16);
    if (*endptr != '\0') return 1;
    if (v < 0 || v >= 256) return 1;
    *bytes++ = (char) v;
  }
  // check that all of the input has been parsed
  if (end != strchr(b, '\0')) return 1;
  return 0;
}

#include <arpa/inet.h>

// is this portable enough?
// from http://stackoverflow.com/questions/3736335/tell-whether-a-text-string-is-an-ipv6-address-or-ipv4-address-using-standard-c-s
static int try_to_parse_ipv6(char *param, char *bytes) {
  return (inet_pton(AF_INET6, param, bytes) == 1) ? 0 : 1;
}

static char char2digit(char c, int *err) {
  *err = 0;
  if (c >= '0' && c <= '9')
    return (c - '0');
  if (c >= 'A' && c <= 'F')
    return (c - 'A' + 10);
  if (c >= 'a' && c <= 'f')
    return (c - 'a' + 10);
  *err = 1;
  return 0;
}

static int hexstr_to_bytes(char *param, char *bytes, size_t max_s) {
  int err = 0;
  size_t idx = 0;
  size_t s = strlen(param);
  if (s >= 2 && param[idx] == '0' && param[idx + 1] == 'x')
    idx += 2;

  memset(bytes, 0, max_s);
  if (((s - idx) + 1) / 2 > max_s) return 1;

  bytes += max_s - (((s - idx) + 1) / 2);

  if ((s - idx) % 2 != 0) {
    char c = char2digit(param[idx++], &err);
    if (err) return 1;
    *bytes++ = c;
  }

  for (; idx < s; ) {
    char c = char2digit(param[idx++], &err) << 4;
    if (err) return 1;
    c += char2digit(param[idx++], &err);
    if (err) return 1;
    *bytes++ = c;
  }

  return 0;
}

int param_to_bytes(const char *param, char *bytes, size_t bitwidth) {
  size_t s = (bitwidth + 7) / 8;
  // making a copy, so that we can call strtok on it
  char param_copy[128];
  strncpy(param_copy, param, sizeof(param_copy));
  if (param_copy[sizeof(param_copy) - 1] != '\0') return 1;
  if (bitwidth == 32) {
    if (!try_to_parse_ipv4(param_copy, bytes)) return 0;
  } else if (bitwidth == 48) {
    if (!try_to_parse_macAddr(param_copy, bytes)) return 0;
  } else if (bitwidth == 128) {
    if (!try_to_parse_ipv6(param_copy, bytes)) return 0;
  }
  return hexstr_to_bytes(param_copy, bytes, s);
}
