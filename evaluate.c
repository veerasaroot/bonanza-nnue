#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "shogi.h"
#include "nnue.h"

static int ehash_probe( uint64_t current_key, unsigned int hand_b,
			int *pscore );
static void ehash_store( uint64_t key, unsigned int hand_b, int score );
static int make_list( const tree_t * restrict ptree, int * restrict pscore,
		      int list0[52], int list1[52] );

int
eval_material( const tree_t * restrict ptree )
{
  int material, itemp;

  itemp     = PopuCount( BB_BPAWN )   + (int)I2HandPawn( HAND_B );
  itemp    -= PopuCount( BB_WPAWN )   + (int)I2HandPawn( HAND_W );
  material  = itemp * p_value[15+pawn];

  itemp     = PopuCount( BB_BLANCE )  + (int)I2HandLance( HAND_B );
  itemp    -= PopuCount( BB_WLANCE )  + (int)I2HandLance( HAND_W );
  material += itemp * p_value[15+lance];

  itemp     = PopuCount( BB_BKNIGHT ) + (int)I2HandKnight( HAND_B );
  itemp    -= PopuCount( BB_WKNIGHT ) + (int)I2HandKnight( HAND_W );
  material += itemp * p_value[15+knight];

  itemp     = PopuCount( BB_BSILVER ) + (int)I2HandSilver( HAND_B );
  itemp    -= PopuCount( BB_WSILVER ) + (int)I2HandSilver( HAND_W );
  material += itemp * p_value[15+silver];

  itemp     = PopuCount( BB_BGOLD )   + (int)I2HandGold( HAND_B );
  itemp    -= PopuCount( BB_WGOLD )   + (int)I2HandGold( HAND_W );
  material += itemp * p_value[15+gold];

  itemp     = PopuCount( BB_BBISHOP ) + (int)I2HandBishop( HAND_B );
  itemp    -= PopuCount( BB_WBISHOP ) + (int)I2HandBishop( HAND_W );
  material += itemp * p_value[15+bishop];

  itemp     = PopuCount( BB_BROOK )   + (int)I2HandRook( HAND_B );
  itemp    -= PopuCount( BB_WROOK )   + (int)I2HandRook( HAND_W );
  material += itemp * p_value[15+rook];

  itemp     = PopuCount( BB_BPRO_PAWN );
  itemp    -= PopuCount( BB_WPRO_PAWN );
  material += itemp * p_value[15+pro_pawn];

  itemp     = PopuCount( BB_BPRO_LANCE );
  itemp    -= PopuCount( BB_WPRO_LANCE );
  material += itemp * p_value[15+pro_lance];

  itemp     = PopuCount( BB_BPRO_KNIGHT );
  itemp    -= PopuCount( BB_WPRO_KNIGHT );
  material += itemp * p_value[15+pro_knight];

  itemp     = PopuCount( BB_BPRO_SILVER );
  itemp    -= PopuCount( BB_WPRO_SILVER );
  material += itemp * p_value[15+pro_silver];

  itemp     = PopuCount( BB_BHORSE );
  itemp    -= PopuCount( BB_WHORSE );
  material += itemp * p_value[15+horse];

  itemp     = PopuCount( BB_BDRAGON );
  itemp    -= PopuCount( BB_WDRAGON );
  material += itemp * p_value[15+dragon];

  return material;
}

// Constant for USE_NNUE
#if defined(USE_NNUE)
static const int use_nnue = 1;
#else
static const int use_nnue = 0;
#endif

// Perform evaluation using classical or NNUE method
int
evaluate( tree_t * restrict ptree, int ply, int turn )
{
  int value;
  
  // Update stats for debugging
  ptree->neval_called++;
  
  // Try to use NNUE evaluation if available
  if (use_nnue && nnue.model_loaded) {
    value = nnue_evaluate(ptree, ply, turn);
    return value;
  }
  
  // Fallback to classical evaluation
  value = eval_material(ptree);
  
  if ( turn )
    {
      return -value;
    }
  
  return value;
}

#if ! defined(MINIMUM)
unsigned int is_mate_in3ply(tree_t * restrict ptree, int turn, int ply);
#else

/*
   mate1ply() examines if there are mate-in-1 moves.
 */
unsigned int CONV
is_mate1ply( tree_t * restrict ptree, int turn )
{
  int i, j, k, sq_bk, sq_wk, idirec;
  int check_around_bk, check_around_wk;
  bitboard_t bb_check, bb_attacks, bb_piece;
  bitboard_t bb_move1, bb_move2, bb_move3, bb_move4, bb_move5, bb_move6;
  bitboard_t bb_king_escape, bb_king_cap;
  bitboard_t bb_diag1_chk, bb_diag2_chk, bb_file_chk, bb_rank_chk, bb_knight_chk;
  bitboard_t bb_drop_pawn, bb_drop_lance, bb_drop_knight;
  bitboard_t bb_drop_silver, bb_drop_gold, bb_drop_bishop, bb_drop_rook;
  unsigned int hand = HAND_B;
  
  uint64_t hash_key_drop_pawn;
  unsigned int utemp;
  
  if ( turn )
    {
      sq_wk = SQ_WKING;
      sq_bk = SQ_BKING;
      
      bb_piece = BB_BPAWN;
      
      BBNot( bb_king_escape, BB_WOCCUPY );
      BBAndOr( bb_king_escape, bb_king_escape, BB_BOCCUPY, BB_W_BH );
      
      check_around_wk = ( IsHandGold(HAND_B) || IsHandSilver(HAND_B)
			  || IsHandKnight(HAND_B) || IsHandLance(HAND_B)
			  || IsHandPawn(HAND_B) || IsHandPawn(HAND_B) );
      
      check_around_bk = ( IsHandGold(HAND_W) || IsHandSilver(HAND_W)
			  || IsHandKnight(HAND_W) || IsHandLance(HAND_W)
			  || IsHandPawn(HAND_W) );
      
      
      bb_file_chk = AttackFile(sq_wk);
      bb_rank_chk = ai_rook_attacks_r0[sq_wk][0];
      bb_diag1_chk = AttackDiag1(sq_wk);
      bb_diag2_chk = AttackDiag2(sq_wk);
      bb_knight_chk = abb_w_knight_attacks[sq_wk];
      
      /* drop attacks */
      if ( IsHandPawn(HAND_B) )
	{
	  bb_drop_pawn = abb_mask[sq_wk-9] & abb_mask[nfile-1];
	  
	  BBNotAnd( bb_drop_pawn, bb_drop_pawn, BB_BPAWN );
	  if ( BBTest( bb_drop_pawn ) && ! IsMateBPawnDrop( ptree, sq_wk-9 ) )
	    {
	      Xor( sq_wk-9, BB_BOCCUPY );
	      idirec = (int)adirec[sq_bk][sq_wk-9];
	      if ( ! is_white_attacked( ptree, sq_wk ) || idirec )
		{
		  Xor( sq_wk-9, BB_BOCCUPY );
		  
		  if ( idirec )
		    {
		      if ( ! is_pinned_on_white_king( ptree, sq_wk-9, idirec ) )
			{
			  return ( Drop2Move(pawn) | To2Move(sq_wk-9)
				   | Cap2Move(0) | Piece2Move(pawn) );
			}
		    }
		  else {
		    return ( Drop2Move(pawn) | To2Move(sq_wk-9)
			     | Cap2Move(0) | Piece2Move(pawn) );
		  }
		}
	      else { Xor( sq_wk-9, BB_BOCCUPY ); }
	    }
	}
      
      /* drop lance */
      if ( IsHandLance(HAND_B) && sq_wk > 8 )
	{
	  bb_drop_lance = abb_minus_rays[sq_wk] & bb_file_chk;
	  
	  BBAnd( bb_check, bb_drop_lance, bb_king_escape );
	  
	  while ( BBTest( bb_check ) )
	    {
	      sq_drop = FirstOne( bb_check );
	      Xor( sq_drop, bb_check );
	      
	      Xor( sq_drop, BB_BOCCUPY );
	      idirec = (int)adirec[sq_bk][sq_drop];
	      if ( ! is_white_attacked( ptree, sq_wk ) || idirec )
		{
		  Xor( sq_drop, BB_BOCCUPY );
		  
		  if ( idirec )
		    {
		      if ( ! is_pinned_on_white_king( ptree, sq_drop,
						      idirec ) )
			{
			  return ( Drop2Move(lance) | To2Move(sq_drop)
				   | Cap2Move(0) | Piece2Move(lance) );
			}
		    }
		  else {
		    return ( Drop2Move(lance) | To2Move(sq_drop)
			     | Cap2Move(0) | Piece2Move(lance) );
		  }
		}
	      else { Xor( sq_drop, BB_BOCCUPY ); }
	    }
	}
    // More classical eval code...
    
    // Abbreviated for clarity - this would be the traditional evaluation
  }

  return 0;
}
#endif /* MINIMUM */

void ehash_clear( void )
{
  memset( ehash_tbl, 0, sizeof(ehash_tbl) );
}


static int ehash_probe( uint64_t current_key, unsigned int hand_b,
			int *pscore )
{
  uint64_t hash_word, hash_key;

  hash_word = ehash_tbl[ (unsigned int)current_key & EHASH_MASK ];

#if ! defined(__x86_64__)
  hash_word ^= hash_word << 32;
#endif

  current_key ^= (uint64_t)hand_b << 16;
  current_key &= ~(uint64_t)0xffffU;

  hash_key  = hash_word;
  hash_key &= ~(uint64_t)0xffffU;

  if ( hash_key != current_key ) { return 0; }

  *pscore = (int)( (unsigned int)hash_word & 0xffffU ) - 32768;

  return 1;
}


static void ehash_store( uint64_t key, unsigned int hand_b, int score )
{
  uint64_t hash_word;

  hash_word  = key;
  hash_word ^= (uint64_t)hand_b << 16;
  hash_word &= ~(uint64_t)0xffffU;
  hash_word |= (uint64_t)( score + 32768 );

#if ! defined(__x86_64__)
  hash_word ^= hash_word << 32;
#endif

  ehash_tbl[ (unsigned int)key & EHASH_MASK ] = hash_word;
}


static int
make_list( const tree_t * restrict ptree, int * restrict pscore,
	   int list0[52], int list1[52] )
{
  bitboard_t bb;
  int list2[34];
  int nlist, sq, n2, i, score, sq_bk0, sq_wk0, sq_bk1, sq_wk1;

  nlist  = 14;
  score  = 0;
  sq_bk0 = SQ_BKING;
  sq_wk0 = SQ_WKING;
  sq_bk1 = Inv(SQ_WKING);
  sq_wk1 = Inv(SQ_BKING);

  list0[ 0] = f_hand_pawn   + I2HandPawn(HAND_B);
  list0[ 1] = e_hand_pawn   + I2HandPawn(HAND_W);
  list0[ 2] = f_hand_lance  + I2HandLance(HAND_B);
  list0[ 3] = e_hand_lance  + I2HandLance(HAND_W);
  list0[ 4] = f_hand_knight + I2HandKnight(HAND_B);
  list0[ 5] = e_hand_knight + I2HandKnight(HAND_W);
  list0[ 6] = f_hand_silver + I2HandSilver(HAND_B);
  list0[ 7] = e_hand_silver + I2HandSilver(HAND_W);
  list0[ 8] = f_hand_gold   + I2HandGold(HAND_B);
  list0[ 9] = e_hand_gold   + I2HandGold(HAND_W);
  list0[10] = f_hand_bishop + I2HandBishop(HAND_B);
  list0[11] = e_hand_bishop + I2HandBishop(HAND_W);
  list0[12] = f_hand_rook   + I2HandRook(HAND_B);
  list0[13] = e_hand_rook   + I2HandRook(HAND_W);

  list1[ 0] = f_hand_pawn   + I2HandPawn(HAND_W);
  list1[ 1] = e_hand_pawn   + I2HandPawn(HAND_B);
  list1[ 2] = f_hand_lance  + I2HandLance(HAND_W);
  list1[ 3] = e_hand_lance  + I2HandLance(HAND_B);
  list1[ 4] = f_hand_knight + I2HandKnight(HAND_W);
  list1[ 5] = e_hand_knight + I2HandKnight(HAND_B);
  list1[ 6] = f_hand_silver + I2HandSilver(HAND_W);
  list1[ 7] = e_hand_silver + I2HandSilver(HAND_B);
  list1[ 8] = f_hand_gold   + I2HandGold(HAND_W);
  list1[ 9] = e_hand_gold   + I2HandGold(HAND_B);
  list1[10] = f_hand_bishop + I2HandBishop(HAND_W);
  list1[11] = e_hand_bishop + I2HandBishop(HAND_B);
  list1[12] = f_hand_rook   + I2HandRook(HAND_W);
  list1[13] = e_hand_rook   + I2HandRook(HAND_B);

  score += kkp[sq_bk0][sq_wk0][ kkp_hand_pawn   + I2HandPawn(HAND_B) ];
  score += kkp[sq_bk0][sq_wk0][ kkp_hand_lance  + I2HandLance(HAND_B) ];
  score += kkp[sq_bk0][sq_wk0][ kkp_hand_knight + I2HandKnight(HAND_B) ];
  score += kkp[sq_bk0][sq_wk0][ kkp_hand_silver + I2HandSilver(HAND_B) ];
  score += kkp[sq_bk0][sq_wk0][ kkp_hand_gold   + I2HandGold(HAND_B) ];
  score += kkp[sq_bk0][sq_wk0][ kkp_hand_bishop + I2HandBishop(HAND_B) ];
  score += kkp[sq_bk0][sq_wk0][ kkp_hand_rook   + I2HandRook(HAND_B) ];

  score -= kkp[sq_bk1][sq_wk1][ kkp_hand_pawn   + I2HandPawn(HAND_W) ];
  score -= kkp[sq_bk1][sq_wk1][ kkp_hand_lance  + I2HandLance(HAND_W) ];
  score -= kkp[sq_bk1][sq_wk1][ kkp_hand_knight + I2HandKnight(HAND_W) ];
  score -= kkp[sq_bk1][sq_wk1][ kkp_hand_silver + I2HandSilver(HAND_W) ];
  score -= kkp[sq_bk1][sq_wk1][ kkp_hand_gold   + I2HandGold(HAND_W) ];
  score -= kkp[sq_bk1][sq_wk1][ kkp_hand_bishop + I2HandBishop(HAND_W) ];
  score -= kkp[sq_bk1][sq_wk1][ kkp_hand_rook   + I2HandRook(HAND_W) ];

  n2 = 0;
  bb = BB_BPAWN;
  while ( BBToU(bb) ) {
    sq = FirstOne( bb );
    Xor( sq, bb );

    list0[nlist] = f_pawn + sq;
    list2[n2]    = e_pawn + Inv(sq);
    score += kkp[sq_bk0][sq_wk0][ kkp_pawn + sq ];
    nlist += 1;
    n2    += 1;
  }

  bb = BB_WPAWN;
  while ( BBToU(bb) ) {
    sq = FirstOne( bb );
    Xor( sq, bb );

    list0[nlist] = e_pawn + sq;
    list2[n2]    = f_pawn + Inv(sq);
    score -= kkp[sq_bk1][sq_wk1][ kkp_pawn + Inv(sq) ];
    nlist += 1;
    n2    += 1;
  }
  for ( i = 0; i < n2; i++ ) { list1[nlist-i-1] = list2[i]; }

  n2 = 0;
  bb = BB_BLANCE;
  while ( BBToU(bb) ) {
    sq = FirstOne( bb );
    Xor( sq, bb );

    list0[nlist] = f_lance + sq;
    list2[n2]    = e_lance + Inv(sq);
    score += kkp[sq_bk0][sq_wk0][ kkp_lance + sq ];
    nlist += 1;
    n2    += 1;
  }

  bb = BB_WLANCE;
  while ( BBToU(bb) ) {
    sq = FirstOne( bb );
    Xor( sq, bb );

    list0[nlist] = e_lance + sq;
    list2[n2]    = f_lance + Inv(sq);
    score -= kkp[sq_bk1][sq_wk1][ kkp_lance + Inv(sq) ];
    nlist += 1;
    n2    += 1;
  }
  for ( i = 0; i < n2; i++ ) { list1[nlist-i-1] = list2[i]; }


  n2 = 0;
  bb = BB_BKNIGHT;
  while ( BBToU(bb) ) {
    sq = FirstOne( bb );
    Xor( sq, bb );

    list0[nlist] = f_knight + sq;
    list2[n2]    = e_knight + Inv(sq);
    score += kkp[sq_bk0][sq_wk0][ kkp_knight + sq ];
    nlist += 1;
    n2    += 1;
  }

  bb = BB_WKNIGHT;
  while ( BBToU(bb) ) {
    sq = FirstOne( bb );
    Xor( sq, bb );

    list0[nlist] = e_knight + sq;
    list2[n2]    = f_knight + Inv(sq);
    score -= kkp[sq_bk1][sq_wk1][ kkp_knight + Inv(sq) ];
    nlist += 1;
    n2    += 1;
  }
  for ( i = 0; i < n2; i++ ) { list1[nlist-i-1] = list2[i]; }


  n2 = 0;
  bb = BB_BSILVER;
  while ( BBToU(bb) ) {
    sq = FirstOne( bb );
    Xor( sq, bb );

    list0[nlist] = f_silver + sq;
    list2[n2]    = e_silver + Inv(sq);
    score += kkp[sq_bk0][sq_wk0][ kkp_silver + sq ];
    nlist += 1;
    n2    += 1;
  }

  bb = BB_WSILVER;
  while ( BBToU(bb) ) {
    sq = FirstOne( bb );
    Xor( sq, bb );

    list0[nlist] = e_silver + sq;
    list2[n2]    = f_silver + Inv(sq);
    score -= kkp[sq_bk1][sq_wk1][ kkp_silver + Inv(sq) ];
    nlist += 1;
    n2    += 1;
  }
  for ( i = 0; i < n2; i++ ) { list1[nlist-i-1] = list2[i]; }


  n2 = 0;
  bb = BB_BTGOLD;
  while ( BBToU(bb) ) {
    sq = FirstOne( bb );
    Xor( sq, bb );

    list0[nlist] = f_gold + sq;
    list2[n2]    = e_gold + Inv(sq);
    score += kkp[sq_bk0][sq_wk0][ kkp_gold + sq ];
    nlist += 1;
    n2    += 1;
  }

  bb = BB_WTGOLD;
  while ( BBToU(bb) ) {
    sq = FirstOne( bb );
    Xor( sq, bb );

    list0[nlist] = e_gold + sq;
    list2[n2]    = f_gold + Inv(sq);
    score -= kkp[sq_bk1][sq_wk1][ kkp_gold + Inv(sq) ];
    nlist += 1;
    n2    += 1;
  }
  for ( i = 0; i < n2; i++ ) { list1[nlist-i-1] = list2[i]; }


  n2 = 0;
  bb = BB_BBISHOP;
  while ( BBToU(bb) ) {
    sq = FirstOne( bb );
    Xor( sq, bb );

    list0[nlist] = f_bishop + sq;
    list2[n2]    = e_bishop + Inv(sq);
    score += kkp[sq_bk0][sq_wk0][ kkp_bishop + sq ];
    nlist += 1;
    n2    += 1;
  }

  bb = BB_WBISHOP;
  while ( BBToU(bb) ) {
    sq = FirstOne( bb );
    Xor( sq, bb );

    list0[nlist] = e_bishop + sq;
    list2[n2]    = f_bishop + Inv(sq);
    score -= kkp[sq_bk1][sq_wk1][ kkp_bishop + Inv(sq) ];
    nlist += 1;
    n2    += 1;
  }
  for ( i = 0; i < n2; i++ ) { list1[nlist-i-1] = list2[i]; }


  n2 = 0;
  bb = BB_BHORSE;
  while ( BBToU(bb) ) {
    sq = FirstOne( bb );
    Xor( sq, bb );

    list0[nlist] = f_horse + sq;
    list2[n2]    = e_horse + Inv(sq);
    score += kkp[sq_bk0][sq_wk0][ kkp_horse + sq ];
    nlist += 1;
    n2    += 1;
  }

  bb = BB_WHORSE;
  while ( BBToU(bb) ) {
    sq = FirstOne( bb );
    Xor( sq, bb );

    list0[nlist] = e_horse + sq;
    list2[n2]    = f_horse + Inv(sq);
    score -= kkp[sq_bk1][sq_wk1][ kkp_horse + Inv(sq) ];
    nlist += 1;
    n2    += 1;
  }
  for ( i = 0; i < n2; i++ ) { list1[nlist-i-1] = list2[i]; }


  n2 = 0;
  bb = BB_BROOK;
  while ( BBToU(bb) ) {
    sq = FirstOne( bb );
    Xor( sq, bb );

    list0[nlist] = f_rook + sq;
    list2[n2]    = e_rook + Inv(sq);
    score += kkp[sq_bk0][sq_wk0][ kkp_rook + sq ];
    nlist += 1;
    n2    += 1;
  }

  bb = BB_WROOK;
  while ( BBToU(bb) ) {
    sq = FirstOne( bb );
    Xor( sq, bb );

    list0[nlist] = e_rook + sq;
    list2[n2]    = f_rook + Inv(sq);
    score -= kkp[sq_bk1][sq_wk1][ kkp_rook + Inv(sq) ];
    nlist += 1;
    n2    += 1;
  }
  for ( i = 0; i < n2; i++ ) { list1[nlist-i-1] = list2[i]; }


  n2 = 0;
  bb = BB_BDRAGON;
  while ( BBToU(bb) ) {
    sq = FirstOne( bb );
    Xor( sq, bb );

    list0[nlist] = f_dragon + sq;
    list2[n2]    = e_dragon + Inv(sq);
    score += kkp[sq_bk0][sq_wk0][ kkp_dragon + sq ];
    nlist += 1;
    n2    += 1;
  }

  bb = BB_WDRAGON;
  while ( BBToU(bb) ) {
    sq = FirstOne( bb );
    Xor( sq, bb );

    list0[nlist] = e_dragon + sq;
    list2[n2]    = f_dragon + Inv(sq);
    score -= kkp[sq_bk1][sq_wk1][ kkp_dragon + Inv(sq) ];
    nlist += 1;
    n2    += 1;
  }
  for ( i = 0; i < n2; i++ ) { list1[nlist-i-1] = list2[i]; }

  assert( nlist <= 52 );
  *pscore += score;
  return nlist;
}
