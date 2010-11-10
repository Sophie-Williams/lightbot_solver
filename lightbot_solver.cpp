#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

static const char* the_map =
  "\x00\x00\x00\x00\x00\x00\x00\x00"
  "\x00\x00\x00\x00\x01\x00\x00\x00"
  "\x82\x02\x02\x04\x02\x03\x02\x00"
  "\x00\x00\x00\x04\x03\x04\x02\x82"
  "\x00\x00\x00\x00\x00\x00\x00\x00";

static const uint8_t startX = 0, startY = 1;

enum direction { N, E, S, W };
static const enum direction startDirection = E;

class square
{
private:
  uint8_t c;
public:
  uint8_t get_height(void) const { return c & 63; }
  int has_light(void) const { return (c & 128) >> 7; }
  int is_lit(void) const { return (c & 64) >> 6; }
  void switch_light(void) { c = (c & ~64) | (~c & 64); }
};

static const char *CMD_CHARS = "RL12*F^_";
enum cmd_e {
  right,
  left,
  f1,
  f2,
  light,
  forward,
  jump,
  nop
};

struct program_t { uint8_t cmds[12 + 8 + 8]; };

/* http://groups.google.com/group/comp.lang.c/browse_thread/thread/a9915080a4424068/ */
/* http://www.jstatsoft.org/v08/i14/paper */
static uint32_t prng_state[] = {198765432,362436069,521288629,88675123,886756453};
static uint32_t xorshift(void)
{
  uint32_t t = prng_state[0]^(prng_state[0]>>7);
  prng_state[0]=prng_state[1]; prng_state[1]=prng_state[2]; prng_state[2]=prng_state[3]; prng_state[3]=prng_state[4];
  prng_state[4]=(prng_state[4]^(prng_state[4]<<6))^(t^(t<<13));
  return (prng_state[1]+prng_state[1]+1)*prng_state[4];
}

class prng_c
{
private:
  uint32_t state;
  uint8_t bits_left;
public:
  prng_c(void) : bits_left(0) { }
  uint32_t give_bits(uint8_t nbits)
  {
    if (bits_left < nbits)
    {
      state = xorshift();
      bits_left = 32;
    }
    uint32_t retval = state & ((1 << nbits) - 1);
    state >>= nbits;
    bits_left -= nbits;
    return retval;
  }
};


static void program_from_string(struct program_t* prg, const char *s)
{
  for (int char_idx = 0; char_idx < 28; char_idx++)
  {
    for (int cmd_idx = 0; cmd_idx < 8; cmd_idx++)
    {
      if (s[char_idx] == CMD_CHARS[cmd_idx])
      {
        prg->cmds[char_idx] = cmd_idx;
        goto found;
      }
    }
    fprintf(stderr, "ERROR unknown command %c\n", s[char_idx]);
    found:;
  }
}

static void program_rnd_fill(struct program_t* prg)
{
  prng_c prng;
  for (int i = 0; i < 12; i++)
    prg->cmds[i] = prng.give_bits(3);
  for (int i = 12; i < 20; i++)
    do
      prg->cmds[i] = prng.give_bits(3);
    while (prg->cmds[i] == f1);
  for (int i = 20; i < 28; i++)
    do
      prg->cmds[i] = prng.give_bits(3);
    while ((prg->cmds[i] == f1) || (prg->cmds[i] == f2));
}

static void program_mutate(struct program_t* prg)
{
  prng_c prng;
  for (int mutation = prng.give_bits(3) + 1; mutation; mutation--)
  {
    int cmd_to_mutate = prng.give_bits(5);
    if (cmd_to_mutate >= 30)
    {
      /* insert instead of overwrite a cmd */
      cmd_to_mutate = prng.give_bits(5);
      if (cmd_to_mutate > 26) cmd_to_mutate -= 26;
      for (int i = 27; i > cmd_to_mutate; i--)
        prg->cmds[i] = prg->cmds[i - 1];
    }
    else if (cmd_to_mutate >= 28)
    {
      /* delete a cmd, place new at end */
      cmd_to_mutate = prng.give_bits(5);
      if (cmd_to_mutate > 26) cmd_to_mutate -= 26;
      for (int i = cmd_to_mutate; i < 27; i++)
        prg->cmds[i] = prg->cmds[i + 1];
      cmd_to_mutate = 27;
    }
    prg->cmds[cmd_to_mutate] = prng.give_bits(3);
  }
}

static int program_is_valid(const struct program_t* prg)
{
  for (int i = 0; i < 28; i++)
    assert(prg->cmds[i] >= 0 && prg->cmds[i] <= 7);
  for (int i = 12; i < 20; i++)
    if (prg->cmds[i] == f1)
      return 0;
  for (int i = 20; i < 28; i++)
    if (prg->cmds[i] == f1 || prg->cmds[i] == f2)
      return 0;
  return 1;
}

static void program_print(const struct program_t* prg)
{
  char temp[28];
  for (int i = 0; i < 28; i++)
    temp[i] = CMD_CHARS[prg->cmds[i]];
  fwrite(temp, 1, 12, stdout);
  putchar(' ');
  fwrite(temp+12, 1, 8, stdout);
  putchar(' ');
  fwrite(temp+20, 1, 8, stdout);
  putchar('\n');
}

struct coord_t { uint8_t x, y; };

static struct coord_t step(const struct coord_t coord, enum direction dir)
{
  struct coord_t result = coord;
  switch(dir)
  {
    case N: if (result.y < 4) result.y++; break;
    case E: if (result.x < 7) result.x++; break;
    case S: if (result.y > 0) result.y--; break;
    case W: if (result.x > 0) result.x--; break;
  }
  return result;
}

/*
 * TODO:
 * reset lights instead of copying map each time
 * use defines for 12, 20 and 28
 */
static int program_execute(const struct program_t* prg)
{
  square map[5][8];
  memcpy(&map[0][0], the_map, sizeof(map));
  enum direction curr_dir = startDirection;
  struct coord_t curr = {startX, startY};
  struct coord_t next;
  uint8_t curr_height, next_height;
  uint8_t return_stack[2];
  uint8_t max_height_reached = 0;
  uint8_t pc = 0;
  uint8_t *return_stack_top = &return_stack[0];
  do
  {
    again:
    switch (prg->cmds[pc])
    {
      case right:
        curr_dir = (enum direction)((curr_dir + 1) & 3); break;
      case left:
        curr_dir = (enum direction)((curr_dir + 3) & 3); break;
      case f1:
        *(return_stack_top++) = pc + 1;
        pc = 12;
        goto again;
      case f2:
        *(return_stack_top++) = pc + 1;
        pc = 20;
        goto again;
      case light:
        if (map[curr.y][curr.x].has_light())
          map[curr.y][curr.x].switch_light();
        break;
      case forward:
      case jump:
        next = step(curr, curr_dir);
        curr_height = map[curr.y][curr.x].get_height();
        next_height = map[next.y][next.x].get_height();
        if (prg->cmds[pc] == forward)
        {
          if (curr_height == next_height)
            curr = next;
          break;
        }
        if ((next_height < curr_height) || (next_height == curr_height + 1))
        {
          if (next_height > max_height_reached)
            max_height_reached = next_height;
          curr = next;
        }
        break;
    }
    if (pc == 19 || pc == 27)
      pc = *(--return_stack_top);
    else
      pc++;
  }
  while (pc != 12);
  int num_lights_lit = map[2][0].is_lit() + map[3][7].is_lit();
  return num_lights_lit << 8 | max_height_reached;
}

static void self_test()
{
  static const struct
  {
    const char* prg;
    uint8_t lights_lit, height_reached;
  }
  test_cases[] =
  { /*             1       2       */
    { "1L^LFR21R2__FFF^L^^_^^FF*L^L", 2, 4 },
    { "**1**11_112*_*L_2^_2F_RF_^FL", 2, 4 }
  };
  printf("Self test:\n");
  for (unsigned test_idx = 0; test_idx < sizeof(test_cases)/sizeof(test_cases[0]); test_idx++)
  {
    struct program_t prg;
    program_from_string(&prg, test_cases[test_idx].prg);
    int result = program_execute(&prg);
    int lights_lit = result >> 8;
    int height_reached = result & 255;
    program_print(&prg);
    printf("%d %d\n", lights_lit, height_reached);
    if ((lights_lit != test_cases[test_idx].lights_lit) ||
        (height_reached != test_cases[test_idx].height_reached))
    {
      printf("Test failed.\n");
    }
    else
    {
      printf("Test succeeded.\n");
    }
  }
}

struct stack_item {
  struct stack_item* next;
  int mutate_cnt;
  int result;
  struct program_t prg;
};

int main(int argc, char** argv)
{
  self_test();
  int num_rnd_tries = 100000;
  int max_mutate_cnt = 10000;
  if (argc > 1)
    num_rnd_tries = atoi(argv[1]);
  if (argc > 2)
    max_mutate_cnt = atoi(argv[2]);
  struct stack_item* top = new stack_item();
  top->next = NULL;
  top->mutate_cnt = 0;
  top->result = 0;
  int prev_result = 0;
  for (;;)
  {
    if (!top->next)
    {
      if (!--num_rnd_tries) break;
      program_rnd_fill(&top->prg);
      prev_result = 0;
    }
    else
    {
      if (top->next->mutate_cnt < max_mutate_cnt)
      {
        top->prg = top->next->prg;
        do program_mutate(&top->prg);
        while (!program_is_valid(&top->prg));
        top->next->mutate_cnt++;
      }
      else
      {
        struct stack_item *next = top->next->next;
        delete top->next;
        top->next = next;
        top->mutate_cnt = 0;
        if (top->next)
          prev_result = top->next->result;
        continue;
      }
    }
    top->result = program_execute(&top->prg);
    if (top->result <= prev_result) continue;
    /**/
    int num_lights_lit = top->result >> 8;
    if (num_lights_lit == 2)
    {
      program_print(&top->prg);
    }
    /**/
    prev_result = top->result;
    struct stack_item *next = new stack_item();
    next->next = top;
    next->mutate_cnt = 0;
    top = next;
  }
  delete top;
  return 0;
}