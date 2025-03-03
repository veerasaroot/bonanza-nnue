/*
  BUG LIST                                                       

  - detection of repetitions can be wrong due to collision of hash keys and
    limitation of history table size.

  - detection of mates fails if all of pseudo-legal evasions are perpetual
    checks.  Father more, inferior evasions, such as unpromotion of
    bishop, rook, and lance at 8th rank, are not counted for the mate
    detection. 

  - detection of perpetual checks fails if one of those inferior
    evasions makes a position that occurred four times.
*/
/*
  TODO:
  - idirec && is_pinned_on_black_king();
  - aifile and airank
  - incheck at quies
  - max legal moves
  - tactical macro
  - out_warning( "A node returns a value lower than mate." ); is obsolate.
  - do_mate in hash
  - pv store to hash
  - no threat
  - use IsDiscover macro
  - change hash_store_pv()
  - dek.c is obsolate.
  - limit time ? ? num
  - hash.bin
  - SHARE to all transition table
 */

 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 #include <signal.h>
 #if defined(_WIN32)
 #  include <fcntl.h>
 #  include <windows.h>
 #else
 #  include <unistd.h>
 #  include <sys/time.h>
 #endif
 #include "shogi.h"
 
 #if defined(USE_NNUE)
 #include "nnue.h"
 #include "usi.h"
 #include "benchmark.h"
 #endif
 
 static int main_child( tree_t * restrict ptree );
 
 // Signal handlers
 static void CONV_CDECL sig_tstp( int dummy );
 static void CONV_CDECL sig_int( int dummy );
 static void CONV_CDECL sig_term( int dummy );
 #if ! defined(_WIN32)
 static void CONV_CDECL sig_usr1( int dummy );
 #endif
 
 int CONV_CDECL
 #if defined(CSASHOGI)
 main( int argc, char *argv[] )
 #else
 main( int argc, char *argv[] )
 #endif
 {
   int iret;
   tree_t * restrict ptree;
 
 #ifdef XBOARD
   Out("feature done=0\n");
 #endif
 #if defined(TLP)
   ptree = tlp_atree_work;
 #else
   ptree = &tree;
 #endif
 
 #if defined(CSASHOGI) && defined(_WIN32)
   FreeConsole();
   if ( argc != 2 || strcmp( argv[1], "csa_shogi" ) )
     {
       MessageBox( NULL,
       "The executable image is not intended\x0d"
       "as an independent program file.\x0d"
       "Execute CSA.EXE instead.",
       str_myname, MB_OK | MB_ICONINFORMATION );
       return EXIT_FAILURE;
     }
 #endif
 
   // Process command-line arguments
   int usi_mode = 0;  // Default to legacy mode
   int benchmark_mode = 0;
   int depth = 12;
 
 #if defined(USE_NNUE)
   // Initialize hash values (for all protocols)
   ini_rand( 3760 );
 
   // Signal handlers
 #if defined(_WIN32)
   signal( SIGINT, sig_int );
   signal( SIGTERM, sig_term );
 #else
   signal( SIGINT, sig_int );
   signal( SIGTERM, sig_term );
   signal( SIGTSTP, sig_tstp );
   signal( SIGUSR1, sig_usr1 );
   signal( SIGPIPE, SIG_IGN );
 #endif
 
   Out( "%s - %s", str_myname, str_version );
   
   // Check for USI or benchmark mode
   for (int i = 1; i < argc; ++i) {
     if (!strcmp(argv[i], "usi")) {
       usi_mode = 1;
     } else if (!strcmp(argv[i], "bench") || !strcmp(argv[i], "benchmark")) {
       benchmark_mode = 1;
       
       // Check for depth parameter
       if (i + 1 < argc && isdigit(argv[i+1][0])) {
         depth = atoi(argv[i+1]);
         i++;
       }
     }
   }
 #endif
 
   if ( ini( ptree ) < 0 )
     {
       out_error( "%s", str_error );
       return EXIT_SUCCESS;
     }
 
 #if defined(USE_NNUE)
   // Run in benchmark mode if specified
   if (benchmark_mode) {
     Out("Running benchmark at depth %d...", depth);
     
     // Try to load NNUE
     if (nnue_init("nn.bin")) {
       Out("NNUE loaded for benchmark");
     } else {
       Out("NNUE loading failed, using classical evaluation");
     }
     
     // Run the benchmark suite
     run_benchmark_suite(benchmark_positions, benchmark_positions_count, depth);
     fin();
     return EXIT_SUCCESS;
   }
   
   // Run in USI mode
   if (usi_mode) {
     usi_loop();
     fin();
     return EXIT_SUCCESS;
   }
 #endif
 
   for ( ;; )
     {
       iret = main_child( ptree );
       if ( iret == -1 )
   {
     out_error( "%s", str_error );
     ShutdownClient;
     break;
   }
       else if ( iret == -2 )
   {
     out_warning( "%s", str_error );
     ShutdownClient;
     continue;
   }
       else if ( iret == -3 ) { break; }
     }
 
   if ( fin() < 0 ) { out_error( "%s", str_error ); }
 
   return EXIT_SUCCESS;
 }
 
 
 static int
 main_child( tree_t * restrict ptree )
 {
   int iret;
 
 #if defined(DEKUNOBOU)
   if ( dek_ngame && ( game_status & mask_game_end ) )
     {
       TlpEnd();
       if ( dek_next_game( ptree ) < 0 )
   {
     out_error( "%s", str_error );
     return -3;
   }
     }
 #endif
 
   /* ponder a move */
   ponder_move = 0;
   iret = ponder( ptree );
   if ( iret < 0 ) { return iret; }
   else if ( game_status & flag_quit ) { return -3; }
 
   /* move prediction succeeded, pondering finished,
      and computer made a move. */
   else if ( iret == 2 ) { return 1; }
 
   /* move prediction failed, pondering aborted,
      and we have opponent's move in input buffer. */
   else if ( ponder_move == MOVE_PONDER_FAILED )
     {
     }
 
   /* pondering is interrupted or ended.
      do nothing until we get next input line. */
   else {
     TlpEnd();
     show_prompt();
   }
 
   
   iret = next_cmdline( 1 );
   if ( iret < 0 ) { return iret; }
   else if ( game_status & flag_quit ) { return -3; }
 
 
   iret = procedure( ptree );
   if ( iret < 0 ) { return iret; }
   else if ( game_status & flag_quit ) { return -3; }
 
   return 1;
 }
 
 // Signal handlers
 static void CONV_CDECL
 sig_tstp( int dummy )
 {
 #if ! defined(_WIN32)
     signal( SIGTSTP, sig_tstp );
 #endif
     
     game_status |= flag_suspend;
     
     Out( "SIGTSTP caught." );
 }
 
 static void CONV_CDECL
 sig_int( int dummy )
 {
 #if ! defined(_WIN32)
     signal( SIGINT, sig_int );
 #endif
     
     game_status |= flag_quit;
     
     root_abort = 1;
     
     Out( "SIGINT caught." );
 }
 
 static void CONV_CDECL
 sig_term( int dummy )
 {
     game_status |= flag_quit;
     
     root_abort = 1;
     
     Out( "SIGTERM caught." );
 }
 
 #if ! defined(_WIN32)
 static void CONV_CDECL
 sig_usr1( int dummy )
 {
     signal( SIGUSR1, sig_usr1 );
     
     game_status |= flag_move_now;
     
     Out( "SIGUSR1 caught." );
 }
 #endif