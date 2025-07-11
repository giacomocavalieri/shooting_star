#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** GAME GRIDS *****************************************************************
 * A grid is represented as a single 16bit number where the 9 most significant
 * bits are 1 if the grid has a star in that position, or 0 if the grid has a
 * dark hole in that position.
 * The remaining bits are just ignored and always set to 0.
 *
 * This makes it extremly easy to deal with grids since they are just integers
 * on the stack and we don't have to deal with allocations on the heap.
 */

typedef uint16_t Grid;

const Grid empty_grid = 0b0000000000000000;
const Grid winning_grid = 0b0000000111101111;
const Grid error_grid = 0b1111111111111111;

const Grid cell1 = 0b0000000100000000;
const Grid cell2 = 0b0000000010000000;
const Grid cell3 = 0b0000000001000000;
const Grid cell4 = 0b0000000000100000;
const Grid cell5 = 0b0000000000010000;
const Grid cell6 = 0b0000000000001000;
const Grid cell7 = 0b0000000000000100;
const Grid cell8 = 0b0000000000000010;
const Grid cell9 = 0b0000000000000001;

typedef enum Outcome { Won, Lost, Continue } Outcome;

static bool is_star(Grid grid, int cell) {
  switch (cell) {
  case 1:
    return cell1 & grid;
  case 2:
    return cell2 & grid;
  case 3:
    return cell3 & grid;
  case 4:
    return cell4 & grid;
  case 5:
    return cell5 & grid;
  case 6:
    return cell6 & grid;
  case 7:
    return cell7 & grid;
  case 8:
    return cell8 & grid;
  case 9:
    return cell9 & grid;
  default:
    return false;
  }
}

/**
 * Given a grid and a cell to explode returns a new grid obtained by exploding
 * that cell according to the rules of shooting stars.
 *
 * Note that if the cell is invalid (that is < 1 or > 9) or not a star, this
 * will return the input grid.
 */
static Grid explode(Grid grid, int cell) {
  if (!is_star(grid, cell)) {
    return grid;
  }

  switch (cell) {
  case 1:
    return 0b0000000110110000 ^ grid;
  case 2:
    return 0b0000000111000000 ^ grid;
  case 3:
    return 0b0000000011011000 ^ grid;
  case 4:
    return 0b0000000100100100 ^ grid;
  case 5:
    return 0b0000000010111010 ^ grid;
  case 6:
    return 0b0000000001001001 ^ grid;
  case 7:
    return 0b0000000000110110 ^ grid;
  case 8:
    return 0b0000000000000111 ^ grid;
  case 9:
    return 0b0000000000011011 ^ grid;
  default:
    return grid;
  }
}

static Outcome outcome(Grid grid) {
  if (grid == empty_grid) {
    return Lost;
  } else if (grid == winning_grid) {
    return Won;
  } else {
    return Continue;
  }
}

/**
 * A sequence of moves leading to a winning configuration.
 * We treat it as an immutable, shared, reference-counted, singly linked list.
 *
 * We represent the empty path as `NULL`.
 */
typedef struct Path {
  int references;
  int move;
  struct Path *rest;
} Path;

static Path *new_path() { return NULL; }

/**
 * This is used to drop a reference to a path. For example when a
 * heap-allocated `QueueNode` is freed, we have to record that it is no longer
 * pointing to the `Path` it was holding onto.
 *
 * Once the references to a path get down to 0, it is automatically freed.
 *
 * Note how this might end up freeing the entire path (complexity linear in its
 * length): say we have a path like this one (the first number is the move, the
 * second number is the number of references):
 *
 *     [1 | 1] -> [2 | 1] -> [9 | 1] -> ø
 *
 * If we free the first node we will have to free al the other ones since
 * there's no other incoming references! However, we never end up freeing a
 * piece of path that might be referenced by someone else. Say we have two paths
 * that share a common suffix:
 *
 *     [1 | 1] -> [2 | 2] -> [9 | 1] -> ø
 *     [5 | 1] ---↗
 *
 * If we free the `[1 | 1]` node we will be left with just a single path with
 * the updated number of references:
 *
 *     [5 | 1] -> [2 | 1] -> [9 | 1] -> ø
 *
 */
static void drop_reference_to_path(Path *path) {
  if (path == NULL) {
    return;
  }

  path->references--;
  if (path->references == 0) {
    // If we get to 0 references it means no one is no longer referencing this
    // path and we can free the node. Since this node points to another one we
    // have to drop a reference to that one as well!
    //
    // So notice how this might end up freeing the entire path if it was not
    // shared by anyone. This is exactly what we want to not leak memory.
    drop_reference_to_path(path->rest);
    free(path);
  }
}

/** Marks that someone else is refering this path. */
static void inc_reference_to_path(Path *path) {
  if (path != NULL) {
    path->references++;
  }
}

/**
 * Returns a new path obtained by adding the move to the top of the given path.
 * Note: the rest of the path is shared, not copied!
 *
 * This is O(1) space and time.
 */
static Path *add_move_to_path(Path *path, int move) {
  Path *new_path = (Path *)malloc(sizeof(Path));
  if (new_path == NULL) {
    return NULL;
  }

  new_path->move = move;
  new_path->references = 1;

  // Since this new node is referencing the `path` we have to increase the
  // references to it.
  new_path->rest = path;
  inc_reference_to_path(path);

  return new_path;
}

/** DOUBLE ENDED QUEUE *********************************************************
 * This is a doubly linked list where we can add elements to an end and get
 * those out from the other in O(1) time.
 */

typedef struct QueueNode {
  Path *path;
  Grid grid;
  struct QueueNode *next;
  struct QueueNode *prev;
} QueueNode;

typedef struct Queue {
  QueueNode *first;
  QueueNode *last;
} Queue;

/**
 * Creates a new empty queue.
 */
static Queue *new_queue() {
  Queue *queue = (Queue *)malloc(sizeof(Queue));
  if (queue != NULL) {
    queue->first = NULL;
    queue->last = NULL;
  }
  return queue;
}

/**
 * Removes a item from the back of the queue and returns a reference to it.
 * Returns `NULL` if the queue is empty.
 */
static QueueNode *pop_back(Queue *queue) {
  if (queue == NULL) {
    return NULL;
  }

  QueueNode *last = queue->last;
  if (last != NULL) {
    queue->last = last->prev;
    if (queue->last == NULL) {
      queue->first = NULL;
    }
  }

  return last;
}

/**
 * Adds a new item to the front of the queue.
 */
static void push_front(Queue *queue, Path *path, Grid grid) {
  if (queue == NULL) {
    return;
  }

  QueueNode *new_first = (QueueNode *)malloc(sizeof(QueueNode));
  if (new_first == NULL) {
    return;
  }

  new_first->prev = NULL;
  new_first->next = queue->first;
  new_first->grid = grid;
  new_first->path = path;

  if (queue->first != NULL) {
    queue->first->prev = new_first;
  }
  queue->first = new_first;
  if (queue->last == NULL) {
    queue->last = new_first;
  }
}

/**
 * Frees a queue node.
 * Note: you have to be very careful when using this. This takes care of
 * cleaning up a single node and does nothing with the other linked nodes.
 * You only want to call this when you have a `QueueNode` you've already popped
 * out of the queue.
 */
static void free_queue_node(QueueNode *node) {
  drop_reference_to_path(node->path);
  free(node);
}

/**
 * Frees an entire queue.
 */
static void free_queue(Queue *queue) {
  if (queue == NULL) {
    return;
  }

  QueueNode *node;
  while ((node = pop_back(queue)) != NULL) {
    free_queue_node(node);
  }
  free(queue);
}

/** THE SOLUTION **************************************************************/

/**
 * Given an initial grid, this returns the shortest path leading to a winning
 * configuration or `NULL` if there isn't one.
 *
 * Note: the path is a linked list that's going to be in reverse order! Meaning
 * if we get a path like this: `1 -> 2 -> 9` the actual sequence of moves
 * should be `9 -> 2 -> 1`.
 */
static Path *shortest_winning_path(Grid initial) {
  // To perform the bfs I'll need to keep track of the grids I've visited. The
  // easiest way to do that is to have an array with a slot for each of the
  // possible grids and set it to 1 when we've visited the corresponding grid.
  //
  // Each grid has 9 slots that can take 2 different states, so there's only
  // 512 possible grids.
  char *visited = (char *)calloc(512, sizeof(bool));
  Queue *to_visit = new_queue();
  if (visited == NULL || to_visit == NULL) {
    free(visited);
    free(to_visit);
    return NULL;
  }

  // We start with just the initial grid as the only node to visit.
  // Note how we push all nodes at the front of the queue and get the next node
  // to visit from the back! This means we're always visiting the nodes closest
  // to the root first, before moving onto those that might be farther away.
  push_front(to_visit, new_path(), initial);
  QueueNode *node = NULL;
  Path *winning_path = NULL;

  while ((node = pop_back(to_visit)) != NULL) {
    // We record that the node has been visited, so we won't visit it again in
    // future iterations.
    visited[node->grid] = true;

    Outcome grid_outcome = outcome(node->grid);
    if (grid_outcome == Won) {
      // If we've found a winning grid we're done! We store the winning path and
      // record that there's a new reference to it.
      winning_path = node->path;
      inc_reference_to_path(winning_path);
    } else if (grid_outcome == Continue) {
      // Otherwise we go throug all the grids that can be reached from this one
      // by making a star explode:
      for (int i = 1; i <= 9; i++) {
        if (is_star(node->grid, i)) {
          Grid new_grid = explode(node->grid, i);
          if (!visited[new_grid]) {
            // If this new grid hasn't been visited yet we add it to the back of
            // the queue of nodes we need to visit with the new updated path
            // that got us there.
            Path *new_path = add_move_to_path(node->path, i);
            push_front(to_visit, new_path, new_grid);
          }
        }
      }
    }

    // After we're done with this node we free it, then if we've found a winning
    // path we know we can stop exploring!
    free_queue_node(node);
    if (winning_path != NULL) {
      break;
    }
  }

  // Before returning we have to free the `visited` cache and the `to_visit`
  // queue with all the other nodes waiting to be visited.
  free_queue(to_visit);
  free(visited);
  return winning_path;
}

/** PRINTING AND PARSING ******************************************************/

static void print_grid(Grid grid) {
  printf("%c%c%c\n%c%c%c\n%c%c%c", cell1 & grid ? '*' : '.',
         cell2 & grid ? '*' : '.', cell3 & grid ? '*' : '.',
         cell4 & grid ? '*' : '.', cell5 & grid ? '*' : '.',
         cell6 & grid ? '*' : '.', cell7 & grid ? '*' : '.',
         cell8 & grid ? '*' : '.', cell9 & grid ? '*' : '.');
}

/**
 * Prints a full text explanation of the sequence of moves leading to victory
 * from a given initial grid.
 * This is a bit verbose and shows all intermediate steps.
 */
static void print_path_loop(Path *path) {
  if (path == NULL) {
    return;
  }
  print_path_loop(path->rest);
  printf("%d\n", path->move);
}

static void print_path(Path *path) {
  if (path == NULL) {
    printf("-1");
  } else {
    print_path_loop(path);
  }
}

/**
 * Turns a string into a grid.
 * Returns an `error_grid` if the given string contains invalid characters or
 * more characters than expected.
 */
static Grid parse(char *input) {
  char a, b, c, d, e, f, g, h, i;
  if (strlen(input) != 11) {
    return error_grid;
  }

  sscanf(input, "%c%c%c\n%c%c%c\n%c%c%c", &a, &b, &c, &d, &e, &f, &g, &h, &i);

  Grid grid = empty_grid;
  grid = a == '*' ? (cell1 | grid) : (a == '.' ? grid : error_grid);
  grid = b == '*' ? (cell2 | grid) : (b == '.' ? grid : error_grid);
  grid = c == '*' ? (cell3 | grid) : (c == '.' ? grid : error_grid);
  grid = d == '*' ? (cell4 | grid) : (d == '.' ? grid : error_grid);
  grid = e == '*' ? (cell5 | grid) : (e == '.' ? grid : error_grid);
  grid = f == '*' ? (cell6 | grid) : (f == '.' ? grid : error_grid);
  grid = g == '*' ? (cell7 | grid) : (g == '.' ? grid : error_grid);
  grid = h == '*' ? (cell8 | grid) : (h == '.' ? grid : error_grid);
  grid = i == '*' ? (cell9 | grid) : (i == '.' ? grid : error_grid);
  return grid;
}

/** PLAYING THE ENTIRE GAME ***************************************************/

typedef enum Mode { Chatty, Silent } Mode;

void play(Grid grid, Mode mode) {
  Path *winning_path = shortest_winning_path(grid);

  if (mode == Chatty) {
    if (winning_path == NULL) {
      printf("There's no winning sequence of moves!\n");
    } else {
      print_path(winning_path);
    }
  }

  drop_reference_to_path(winning_path);
}

int main() {
  // We play all possible games in silent mode to check if we can ever leak any
  // memory.
  // for (int grid = empty_grid; grid < 512; grid++) {
  //  play(grid, Silent);
  //}

  play(0b100000000, Chatty);
}
