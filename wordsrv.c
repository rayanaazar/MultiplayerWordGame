#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <time.h>

#include "socket.h"
#include "gameplay.h"

#ifndef PORT
#define PORT 57338
#endif
#define MAX_QUEUE 5

/*Change the game's has_next_turn to NULL 
if all the players quit. 
*/
void all_players_quit(struct game_state *game)
{
    if (game->head == NULL)
    {
        game->has_next_turn = NULL;
    }
}

void add_player(struct client **top, int fd, struct in_addr addr);
void remove_player(struct client **top, int fd);
void announce_disconect(struct game_state *game, struct client *gone_player);
void advance_turn(struct game_state *game);
/* These are some of the function prototypes that we used in our solution 
 * You are not required to write functions that match these prototypes, but
 * you may find the helpful when thinking about operations in your program.
 */
/* Send the message in outbuf to all clients */
void broadcast(struct game_state *game, char *outbuf)
{
    for (struct client *player = game->head; player != NULL; player = player->next)
    {
        if (write(player->fd, outbuf, strlen(outbuf)) == -1)
        {
            fprintf(stderr, "Write to client %s failed\n", inet_ntoa(player->ipaddr));
            announce_disconect(game, player);
            remove_player(&(game->head), player->fd);
            all_players_quit(game);
        }
    }
}

/*Announce the turn of the player that is about to play, 
and prompt him his choices. 
*/
void announce_turn(struct game_state *game, int invalid)
{
    char whos_turn[MAX_BUF];
    sprintf(whos_turn, "It's %s's turn.\n", (game->has_next_turn)->name);
    for (struct client *player = game->head; player != NULL; player = player->next)
    {
        if (((player->fd) != ((game->has_next_turn)->fd)) && (invalid == 0))
        {
            if (write(player->fd, whos_turn, strlen(whos_turn)) == -1)
            {
                fprintf(stderr, "Write to client %s failed\n", inet_ntoa(player->ipaddr));
                announce_disconect(game, player);
                remove_player(&(game->head), player->fd);
                all_players_quit(game);
            }
        }
        if ((player->fd) == ((game->has_next_turn)->fd))
        {
            char *guess_please = "Your guess?\n";

            if (write(player->fd, guess_please, strlen(guess_please)) == -1)
            {
                fprintf(stderr, "Write to client %s failed\n", inet_ntoa(player->ipaddr));
                advance_turn(game);
                announce_disconect(game, player);
                remove_player(&(game->head), player->fd);
                all_players_quit(game);
            }
        }
    }
}

/*Announce the player that is disconected, 
and prompt the next player to play. 
*/
void announce_disconect(struct game_state *game, struct client *gone_player)
{
    char notconnected[MAX_MSG];
    sprintf(notconnected, "GoodBye %s! Now disconnected.\n", gone_player->name);
    for (struct client *player = game->head; player != NULL; player = player->next)
    {
        if (player->fd != gone_player->fd)
        {
            if (write(player->fd, notconnected, strlen(notconnected)) == -1)
            {
                fprintf(stderr, "Write to client %s failed\n", inet_ntoa(player->ipaddr));
                announce_disconect(game, player);
                remove_player(&(game->head), player->fd);
                all_players_quit(game);
            }
        }
    }
    announce_turn(game, 0);
}

/*Advance the turn of the game, so that the next player
can play.
*/
void advance_turn(struct game_state *game)
{
    if ((game->head)->next == NULL)
    {
        game->has_next_turn = game->has_next_turn;
    }
    if (((game->has_next_turn)->next) == NULL)
    {
        game->has_next_turn = game->head;
    }
    else
    {
        game->has_next_turn = (game->has_next_turn)->next;
    }
}
/*Announce the winner of the game to all players. 
*/
void announce_winner(struct game_state *game, struct client *winner)
{
    for (struct client *player = game->head; player != NULL; player = player->next)
    {
        char theword[MAX_MSG];
        char thewinner[MAX_MSG];
        sprintf(theword, "The word was %s.\n", game->word);
        sprintf(thewinner, "Game over! %s won!\n", winner->name);

        printf("Game over! %s won!\n", winner->name);

        if (write(player->fd, theword, strlen(theword)) == -1)
        {
            fprintf(stderr, "Write to client %s failed\n", inet_ntoa(player->ipaddr));
            announce_disconect(game, player);
            remove_player(&(game->head), player->fd);
            all_players_quit(game);
        }

        if (player->fd == winner->fd)
        {
            char *win = "Game over!You win!\n";
            if (write(player->fd, win, strlen(win)) == -1)
            {
                fprintf(stderr, "Write to client %s failed\n", inet_ntoa(player->ipaddr));
                advance_turn(game);
                announce_disconect(game, player);
                remove_player(&(game->head), player->fd);
                all_players_quit(game);
            }
        }
        else
        {
            if (write(player->fd, thewinner, strlen(thewinner)) == -1)
            {
                fprintf(stderr, "Write to client %s failed\n", inet_ntoa(player->ipaddr));
                announce_disconect(game, player);
                remove_player(&(game->head), player->fd);
                all_players_quit(game);
            }
        }
    }
}
/* Prompt a player trying to type, while 
it is not his turn and he can't type.
*/
void cant_type(struct game_state *game, struct client *type_player)
{
    char *notype = "You can't type. It is not your turn.\n";
    if (write(type_player->fd, notype, strlen(notype)) == -1)
    {
        fprintf(stderr, "Write to client %s failed\n", inet_ntoa(type_player->ipaddr));
        announce_disconect(game, type_player);
        remove_player(&(game->head), type_player->fd);
        all_players_quit(game);
    }
}

/* Move the has_next_turn pointer to the next active client */

/* The set of socket descriptors for select to monitor.
 * This is a global variable because we need to remove socket descriptors
 * from allset when a write to a socket fails.
 */
fd_set allset;

/* Add a client to the head of the linked list
 */
void add_player(struct client **top, int fd, struct in_addr addr)
{
    struct client *p = malloc(sizeof(struct client));

    if (!p)
    {
        perror("malloc");
        exit(1);
    }

    printf("Adding client %s\n", inet_ntoa(addr));

    p->fd = fd;
    p->ipaddr = addr;
    p->name[0] = '\0';
    p->in_ptr = p->inbuf;
    p->inbuf[0] = '\0';
    p->next = *top;
    *top = p;
}

/* Removes client from the linked list and closes its socket.
 * Also removes socket descriptor from allset 
 */
void remove_player(struct client **top, int fd)
{
    struct client **p;

    for (p = top; *p && (*p)->fd != fd; p = &(*p)->next)
        ;
    // Now, p points to (1) top, or (2) a pointer to another client
    // This avoids a special case for removing the head of the list
    if (*p)
    {
        struct client *t = (*p)->next;
        printf("Removing client %d %s\n", fd, inet_ntoa((*p)->ipaddr));
        FD_CLR((*p)->fd, &allset);
        close((*p)->fd);
        free(*p);
        *p = t;
    }
    else
    {
        fprintf(stderr, "Trying to remove fd %d, but I don't know about it\n",
                fd);
    }
}

int find_network_newline(const char *buf, int n)
{
    for (int i = 0; i < n; i++)
    {
        if ((buf[i] == '\n') && (buf[i - 1] == '\r'))
        {
            return i - 1;
        }
    }

    return -1;
}

int main(int argc, char **argv)
{
    int clientfd, maxfd, nready;
    struct client *p;
    struct sockaddr_in q;
    fd_set rset;

    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s <dictionary filename>\n", argv[0]);
        exit(1);
    }

    // Create and initialize the game state
    struct game_state game;

    srandom((unsigned int)time(NULL));
    // Set up the file pointer outside of init_game because we want to
    // just rewind the file when we need to pick a new word
    game.dict.fp = NULL;
    game.dict.size = get_file_length(argv[1]);

    init_game(&game, argv[1]);

    // head and has_next_turn also don't change when a subsequent game is
    // started so we initialize them here.
    game.head = NULL;
    game.has_next_turn = NULL;

    /* A list of client who have not yet entered their name.  This list is
     * kept separate from the list of active players in the game, because
     * until the new playrs have entered a name, they should not have a turn
     * or receive broadcast messages.  In other words, they can't play until
     * they have a name.
     */
    struct client *new_players = NULL;

    struct sockaddr_in *server = init_server_addr(PORT);
    int listenfd = set_up_server_socket(server, MAX_QUEUE);

    // initialize allset and add listenfd to the
    // set of file descriptors passed into select
    FD_ZERO(&allset);
    FD_SET(listenfd, &allset);
    // maxfd identifies how far into the set to search
    maxfd = listenfd;

    while (1)
    {
        // make a copy of the set before we pass it into select
        rset = allset;
        nready = select(maxfd + 1, &rset, NULL, NULL, NULL);
        if (nready == -1)
        {
            perror("select");
            continue;
        }

        if (FD_ISSET(listenfd, &rset))
        {
            printf("A new client is connecting\n");
            clientfd = accept_connection(listenfd);

            FD_SET(clientfd, &allset);
            if (clientfd > maxfd)
            {
                maxfd = clientfd;
            }
            printf("Connection from %s\n", inet_ntoa(q.sin_addr));
            add_player(&new_players, clientfd, q.sin_addr);
            char *greeting = WELCOME_MSG;
            if (write(clientfd, greeting, strlen(greeting)) == -1)
            {
                fprintf(stderr, "Write to client %s failed\n", inet_ntoa(q.sin_addr));
                remove_player(&(game.head), p->fd);
            };
        }

        /* Check which other socket descriptors have something ready to read.
         * The reason we iterate over the rset descriptors at the top level and
         * search through the two lists of clients each time is that it is
         * possible that a client will be removed in the middle of one of the
         * operations. This is also why we call break after handling the input.
         * If a client has been removed the loop variables may not longer be 
         * valid.
         */
        int cur_fd;
        for (cur_fd = 0; cur_fd <= maxfd; cur_fd++)
        {
            if (FD_ISSET(cur_fd, &rset))
            {
                // Check if this socket descriptor is an active player
                for (p = game.head; p != NULL; p = p->next)
                {

                    if (cur_fd == p->fd)
                    {

                        if (p->fd == game.has_next_turn->fd)
                        {

                            //The player whose turn it is is GUESSING A LETTER.
                            char letter;
                            int rbytes = read(p->fd, p->in_ptr, (MAX_BUF - (p->in_ptr - p->inbuf)));
                            if (rbytes <= 0)
                            {
                                fprintf(stderr, "Read from client %s failed\n", inet_ntoa(p->ipaddr));
                                advance_turn(&game);
                                announce_disconect(&game, p);
                                remove_player(&(game.head), p->fd);
                                all_players_quit(&game);
                            }
                            printf("[%d] Read %d bytes\n", p->fd, rbytes);

                            p->in_ptr = p->in_ptr + rbytes;
                            int numletters = p->in_ptr - p->inbuf;

                            int index = find_network_newline(p->inbuf, numletters); // find the the network newline char.

                            if (index >= 0)
                            {
                                p->inbuf[index] = '\0';
                                letter = p->inbuf[0];
                                p->in_ptr = p->inbuf;

                                printf("[%d] Found newline %c\n", p->fd, letter);

                                // CHECKS IF THE LETTER IS VALID
                                int invalid = 0;
                                if ((letter < 'a') || (letter > 'z') || (game.letters_guessed[letter - 'a'] == 1) || (strlen(p->inbuf) != 1))
                                {
                                    invalid = 1;
                                    char *lower_msg = "Invalid letter OR already guessed letter.\n";
                                    if (write(p->fd, lower_msg, strlen(lower_msg)) == -1)
                                    {
                                        fprintf(stderr, "Write to client %s failed\n", inet_ntoa(p->ipaddr));
                                        advance_turn(&game);
                                        announce_disconect(&game, p);
                                        remove_player(&(game.head), p->fd);
                                        all_players_quit(&game);
                                    }
                                }
                                else
                                {
                                    game.letters_guessed[letter - 'a'] = 1;

                                    for (struct client *player = game.head; player != NULL; player = player->next)
                                    {
                                        char p_guessed[MAX_BUF];
                                        sprintf(p_guessed, "%s guesses : %c\n", p->name, letter);
                                        if (write(player->fd, p_guessed, strlen(p_guessed)) == -1)
                                        {
                                            fprintf(stderr, "Write to client %s failed\n", inet_ntoa(player->ipaddr));
                                            advance_turn(&game);
                                            announce_disconect(&game, player);
                                            remove_player(&(game.head), player->fd);
                                            all_players_quit(&game);
                                        }
                                    }

                                    // SEARCH THE LETTER IN THE WORD AND UPDATE THE GAME STATE.

                                    int size_word = sizeof(game.word);
                                    int found = 0;
                                    for (int i = 0; i < size_word; i++)
                                    {
                                        if (game.word[i] == letter)
                                        {
                                            game.guess[i] = letter;
                                            found = found + 1;
                                        }
                                    }

                                    if (found != 0)
                                    {
                                        int eq = strcmp(game.word, game.guess);
                                        if (eq == 0)
                                        {
                                            announce_winner(&game, p);
                                            init_game(&game, argv[1]);
                                            printf("NEW GAME\n");
                                        }
                                        char *newgame = "Let's start a new game\n";
                                        broadcast(&game, newgame);

                                        char mesg[MAX_MSG];
                                        char *status_update = status_message(mesg, &game);
                                        broadcast(&game, status_update);
                                        printf("It's %s's turn.\n", game.has_next_turn->name);
                                    }
                                    else
                                    {
                                        char notfound[MAX_MSG];
                                        sprintf(notfound, "%c is not in the word\n", letter);
                                        if (write(p->fd, notfound, strlen(notfound)) == -1)
                                        {
                                            fprintf(stderr, "Write to client %s failed\n", inet_ntoa(p->ipaddr));
                                            advance_turn(&game);
                                            announce_disconect(&game, p);
                                            remove_player(&(game.head), p->fd);
                                            all_players_quit(&game);
                                        }

                                        printf("Letter %c is not in the word\n", letter);
                                        game.guesses_left -= 1;

                                        if (game.guesses_left <= 0)
                                        {
                                            char endgame[MAX_MSG] = "No guesses left. Game over.\n\n\n";
                                            broadcast(&game, endgame);
                                            init_game(&game, argv[1]);
                                            printf("NEW GAME\n");
                                        }
                                        char *newgame = "Let's start a new game\n";
                                        broadcast(&game, newgame);
                                        char mesg[MAX_MSG];
                                        char *status_update = status_message(mesg, &game);
                                        broadcast(&game, status_update);
                                        advance_turn(&game);
                                        printf("It's %s's turn.\n", game.has_next_turn->name);
                                    }
                                }

                                announce_turn(&game, invalid);
                            }
                        }

                        else // READ THE INPUT OF OTHER PLAYERS THAT ARE NOT SUPPOSE TO TYPE.
                        {
                            int garbage = read(p->fd, p->in_ptr, (MAX_BUF - (p->in_ptr - p->inbuf)));
                            if (garbage <= 0)
                            {
                                fprintf(stderr, "Read from client %s failed\n", inet_ntoa(p->ipaddr));
                                announce_disconect(&game, p);
                                remove_player(&(game.head), p->fd);
                                all_players_quit(&game);
                            }
                            p->in_ptr = p->in_ptr + garbage;
                            int num = p->in_ptr - p->inbuf;

                            int ind = find_network_newline(p->inbuf, num);

                            if (ind >= 0)
                            {
                                cant_type(&game, p);
                            }
                            p->in_ptr = p->inbuf; // discard whatever was read.
                        }

                        break;
                    }
                }

                // Check if any new players are entering their names
                for (p = new_players; p != NULL; p = p->next)
                {
                    if (cur_fd == p->fd)
                    {

                        int nbytes = read(p->fd, p->in_ptr, (MAX_BUF - (p->in_ptr - p->inbuf)));
                        if (nbytes <= 0)
                        {
                            fprintf(stderr, "Read from client %s failed\n", inet_ntoa(p->ipaddr));
                            remove_player(&new_players, p->fd);
                        }
                        printf("[%d] Read %d bytes\n", p->fd, nbytes);

                        p->in_ptr = p->in_ptr + nbytes;
                        int numofletters = p->in_ptr - p->inbuf;

                        int r_index = find_network_newline(p->inbuf, numofletters); // find the network newline char.

                        if (r_index >= 0)
                        {
                            strncpy(p->name, p->inbuf, MAX_NAME);

                            p->name[r_index] = '\0';

                            printf("[%d] Found newline %s\n", p->fd, p->name);

                            p->in_ptr = p->inbuf;

                            int valid = 0;

                            //CHECK IF NAME IS EMPTY.
                            if (p->name[0] == '\0')
                            {
                                valid = 1;
                                char *greeting = "Invalid name. Type another name. ";
                                if (write(p->fd, greeting, strlen(greeting)) == -1)
                                {
                                    fprintf(stderr, "Write to client %s failed\n", inet_ntoa(p->ipaddr));
                                    remove_player(&new_players, p->fd);
                                }
                            }

                            //CHECK IF NAME IS ALREADY TAKEN.
                            else
                            {
                                for (struct client *other_p = game.head; other_p != NULL; other_p = other_p->next)
                                {
                                    if ((valid != 1) && ((strcmp(other_p->name, p->name)) == 0))
                                    {
                                        valid = 1;
                                        char *greeting = "Invalid name. Type another name. ";
                                        if (write(p->fd, greeting, strlen(greeting)) == -1)
                                        {
                                            fprintf(stderr, "Write to client %s failed\n", inet_ntoa(p->ipaddr));
                                            remove_player(&new_players, p->fd);
                                        }
                                    }
                                }
                            }

                            if (valid == 0)
                            {
                                int start_game = 0;
                                if (game.head == NULL)
                                {
                                    game.has_next_turn = p;
                                    start_game = 1;
                                }

                                //Removing the player from the new_player list.
                                if (new_players->fd == p->fd)
                                {
                                    new_players = new_players->next;
                                }
                                else
                                {
                                    for (struct client *plyr = new_players; plyr != NULL; plyr = plyr->next)
                                    {
                                        if (plyr->next == p)
                                        {
                                            plyr->next = (plyr->next)->next;
                                        }
                                    }
                                }

                                p->next = game.head; // if the name was valid add the player to active list.
                                game.head = p;

                                char p_joined_msg[MAX_BUF];
                                sprintf(p_joined_msg, "%s has just joined.\n", p->name);
                                broadcast(&game, p_joined_msg); // tell all players that a player has joined.

                                char mesg[MAX_MSG];
                                char *status_update = status_message(mesg, &game);

                                if (write(p->fd, status_update, strlen(status_update)) == -1)
                                {
                                    fprintf(stderr, "Write to client %s failed\n", inet_ntoa(p->ipaddr));
                                    remove_player(&(game.head), p->fd);
                                    all_players_quit(&game);
                                }

                                if (start_game == 1) // If game just started.
                                {
                                    char *guess_please = "Your guess?\n";

                                    printf("It's %s's turn.\n", p->name);

                                    if (write(p->fd, guess_please, strlen(guess_please)) == -1)
                                    {
                                        fprintf(stderr, "Write to client %s failed\n", inet_ntoa(p->ipaddr));
                                        remove_player(&(game.head), p->fd);
                                        all_players_quit(&game);
                                    }
                                    start_game = 0;
                                }

                                else
                                {
                                    char *guess_please = "Your guess?\n";

                                    if (write(game.has_next_turn->fd, guess_please, strlen(guess_please)) == -1)
                                    {
                                        fprintf(stderr, "Write to client %s failed\n", inet_ntoa(game.has_next_turn->ipaddr));
                                        advance_turn(&game);
                                        announce_disconect(&game, game.has_next_turn);
                                        remove_player(&(game.head), game.has_next_turn->fd);
                                        all_players_quit(&game);
                                    }

                                    char msg[MAX_MSG];
                                    sprintf(msg, "It's %s's turn.\n", game.has_next_turn->name);

                                    printf("It's %s's turn.\n", game.has_next_turn->name);

                                    if (write(p->fd, msg, strlen(msg)) == -1)
                                    {
                                        fprintf(stderr, "Write to client %s failed\n", inet_ntoa(p->ipaddr));
                                        announce_disconect(&game, p);
                                        remove_player(&(game.head), p->fd);
                                        all_players_quit(&game);
                                    }
                                }
                            }
                        }
                        break;
                    }
                }
            }
        }
    }

    return 0;
}
