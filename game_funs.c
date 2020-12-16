#include <ftw.h>
#include <ncurses.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include "game_funs.h"
#include "types.h"

void move_to(char *cmd, gameState_t *game, WINDOW *win) {
    unsigned x;
    unsigned curr = game->player_position;
    unsigned n = game->n;
    if (sscanf(cmd, "move-to %u", &x) <= 0)
        return;

    wborder(win, 0, 0, 0, 0, 0, 0, 0, 0);

    if (x < n && game->rooms_map[x * n + curr] == 1) {
        game->player_position = x;
        mvwprintw(win, getmaxy(win) - 1, getmaxx(win) / 2 - 9, " Moved to room %u ", x);
        print_curr_room(x, n, win);
    } else if (x >= n) {
        mvwprintw(win, getmaxy(win) - 1, getmaxx(win) / 2 - 12, " Room %u doesn't exist ", x);
    } else {
        mvwprintw(win, getmaxy(win) - 1, getmaxx(win) / 2 - 12, " Cannot move to room %u ", x);

    }

    wrefresh(win);
}

void pick_up(char *cmd, gameState_t *game, WINDOW *win) {
    unsigned y;
    if (sscanf(cmd, "pick-up %u", &y) <= 0)
        return;

    if (game->num_player_objects == 2) {
        mvwprintw(win, getmaxy(win) - 1, getmaxx(win) / 2 - 20, " Cannot pick up object: inventory full ");
        wrefresh(win);
        return;
    }

    object_t *obj = NULL;
    unsigned i;
    for (i = 0; i < game->rooms[game->player_position].num_existing_objects; i++) {
        if (game->rooms[game->player_position].objects[i]->id == y) {
            obj = game->rooms[game->player_position].objects[i];
            break;
        }
    }

    if (obj == NULL) {
        mvwprintw(win, getmaxy(win) - 1, getmaxx(win) / 2 - 20, " Object with id %u doesn't exist here ", y);
        wrefresh(win);
        return;
    }

    if (i == 0)
        game->rooms[game->player_position].objects[0] = game->rooms[game->player_position].objects[1];

    game->rooms[game->player_position].objects[1] = NULL;
    game->rooms[game->player_position].num_existing_objects--;
    game->player_objects[game->num_player_objects++] = obj;

    print_game(game, win);
    wrefresh(win);
}

void drop(char *cmd, gameState_t *game, WINDOW *win) {
    unsigned pos = game->player_position;

    unsigned z;
    if (sscanf(cmd, "drop %u", &z) <= 0)
        return;

    if (game->num_player_objects == 0) {
        mvwprintw(win, getmaxy(win) - 1, getmaxx(win) / 2 - 9, " Empty inventory ");
        wrefresh(win);
        return;
    }

    object_t *obj = NULL;
    unsigned i;
    for (i = 0; i < game->num_player_objects; i++) {
        if (game->player_objects[i]->id == z) {
            obj = game->player_objects[i];
            break;
        }
    }

    if (obj == NULL) {
        mvwprintw(win, getmaxy(win) - 1, getmaxx(win) / 2 - 19, " Object with id %u not in inventory ", z);
        wrefresh(win);
        return;
    }

    if (game->rooms[pos].num_existing_objects == 2) {
        mvwprintw(win, getmaxy(win) - 1, getmaxx(win) / 2 - 17, " Already two objects in this room ");
        wrefresh(win);
        return;
    }

    if (i == 0) {
        game->player_objects[0] = game->player_objects[1];
    }

    game->player_objects[1] = NULL;
    game->num_player_objects--;
    game->rooms[pos].objects[game->rooms[pos].num_existing_objects++] = obj;

    print_game(game, win);
}

void save(char *cmd, gameState_t *game, WINDOW *win) {
    char path[strlen(cmd)];
    unsigned n = game->n;

    if (sscanf(cmd, "save %s", path) <= 0)
        return;

    int f = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_APPEND, 0777);
    if (f < 0)
        ERROR("Couldn't open file");

    if (write(f, &n, sizeof(unsigned)) < 0)
        ERROR("Couldn't write n");
    if (write(f, game->rooms_map, n*n * sizeof(char)) < 0)
        ERROR("Couldn't write graph to file");

    if (write(f, &game->player_position, sizeof(unsigned)) < 0)
        ERROR("Couldn't write player position");
    if (write(f, &game->num_player_objects, sizeof(unsigned)) < 0)
        ERROR("Couldn't write inventory object count");
    for (unsigned i = 0; i < game->num_player_objects; i++)
        if (write(f, game->player_objects[i], sizeof(object_t)) < 0)
            ERROR("Couldn't write object");

    for (unsigned i = 0; i < n; i++) {
        if (write(f, &i, sizeof(unsigned)) < 0)
            ERROR("Couldn't write room id");
        if (write(f, &game->rooms[i].num_existing_objects, sizeof(unsigned)) < 0)
            ERROR("Couldn't write object count");
        if (write(f, &game->rooms[i].num_assigned_objects, sizeof(unsigned)) < 0)
            ERROR("Couldn't write assigned object count");
        for (unsigned j = 0; j < game->rooms[i].num_existing_objects; j++)
            if (write(f, game->rooms[i].objects[j], sizeof(object_t)) < 0)
                ERROR("Couldn't write object");
    }

    close(f);
    print_game(game, win);
}

void find_path(char *cmd, gameState_t *game, WINDOW *win) {
    unsigned k, x;

    if (sscanf(cmd, "find-path %u %u", &k, &x) <= 0)
        return;

    pthread_t tids[k];
    pathFind_t args[k];

    for (unsigned i = 0; i < k; i++) {
        args[i].game = game;
        args[i].seed = rand();
        args[i].destination = x;
        if (pthread_create(&tids[i], NULL, find_path_worker, &args[i]))
            ERROR("Couldn't create worker thread");
    }

    for (unsigned i = 0; i < k; i++)
        if (pthread_join(tids[i], NULL))
            ERROR("Couldn't join worker thread");

    unsigned shortest_id = 0;
    unsigned shortest_length = args[0].length;

    for (unsigned i = 1; i < k; i++) {
        if (args[i].length < shortest_length) {
            shortest_length = args[i].length;
            shortest_id = i;
        }
    }

    print_game(game, win);

    if (shortest_length == 2000) {
        mvwprintw(win, getmaxy(win) - 1, getmaxx(win) / 2 - 11, " Couldn't find a path ");
        wrefresh(win);
        for (unsigned i = 0; i < k; i++)
            free(args[i].path);
        return;
    }

    mvwprintw(win, getmaxy(win) - 3, 2, "Found path from %u to %u: ", game->player_position, x);
    for (unsigned i = 0; i < shortest_length; i++)
        mvwprintw(win, getmaxy(win) - 3, 35 + 3*i, "%2u ", args[shortest_id].path[i]);
    wrefresh(win);

    for (unsigned i = 0; i < k; i++)
        free(args[i].path);
}

void *find_path_worker(void *voidArgs) {
    pathFind_t *args = voidArgs;

    gameState_t *game = args->game;
    unsigned n = game->n;
    unsigned seed = args->seed;
    unsigned length = 0;
    unsigned curr = game->player_position;
    unsigned destination = args->destination;
    unsigned *path;

    if ((path = malloc(1000 * sizeof(unsigned))) == NULL)
        ERROR("Couldn't allocate path");

    while (curr != destination && length < 1000) {
        unsigned next;
        do {
            next = rand_r(&seed) % n;
        } while (game->rooms_map[next * n + curr] == 0);
        path[length++] = next;
        curr = next;
    }

    args->length = (curr == destination ? length : 2000);
    args->path = path;

    return NULL;
}

void quit(gameState_t *game, WINDOW *win) {
    SET_GAME_MODE(0);

    for (unsigned i = 0; i < game->n; i++) {
        for (unsigned j = 0; j < game->rooms[i].num_existing_objects; j++) {
            free(game->rooms[i].objects[j]);
        }
    }

    for (unsigned i = 0; i < game->num_player_objects; i++)
        free(game->player_objects[i]);

    free(game->rooms);
    free(game->rooms_map);

    werase(win);
    wborder(win, 0, 0, 0, 0, 0, 0, 0, 0);
    wrefresh(win);
}
