#include "agent/agent.h"
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INPUT_BUF 4096

int main(void) {
  config_init();

  Agent *a = agent_create();
  if (!a) {
    fprintf(stderr, "agent_create failed\n");
    return 1;
  }

  char input[INPUT_BUF];
  int rc = 0;
  while (fgets(input, sizeof(input), stdin)) {
    size_t len = strlen(input);
    if (len > 0 && input[len - 1] == '\n')
      input[len - 1] = '\0';

    if (strcmp(input, "exit") == 0)
      break;

    const char *reply = agent_chat(a, input);
    if (!reply) {
      rc = 1;
      break;
    }
    printf("%s\n", reply);
  }

  agent_free(a);
  return rc;
}
