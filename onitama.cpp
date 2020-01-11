// Onitama search

#include <iostream>
#include <unordered_map>
#include <vector>
#include <string>
#include <cstdint>
#include <cassert>
#include <random>
#include <algorithm>
#include <thread>
#include <ctime>
#include <atomic>

#define USE_TABLE
//#define USE_KILLER

std::random_device rd; 
std::mt19937 rng(rd()); // Ugh, only 32 bits of seed.
//std::mt19937 rng(10001);

enum Player : uint8_t {
	WHITE  = 0,
	BLACK  = 1,
	NOBODY = 2,
};

typedef uint8_t Card;
typedef uint8_t Square;
typedef int SquareDelta;

constexpr Square PIECE_CAPTURED = 128;
constexpr int MAX_LEGAL_MOVES = 4 * 5 * 2;
constexpr int SCORE_INF = 1000000;

static inline SquareDelta offset_to_delta(std::pair<int, int> p) {
	return p.first + 8 * p.second;
}

static inline std::pair<int, int> delta_to_offset(SquareDelta sd) {
	return {sd % 8, sd / 8};
}

std::vector<std::vector<std::pair<int, int>>> cards_source{
	// Rabbit
	{{-1, -1}, {1, 1}, {2, 0}},
	// Cobra
	{{-1, 0}, {1, 1}, {1, -1}},
	// Rooster
	{{-1, 0}, {-1, -1}, {1, 0}, {1, 1}},
	// Tiger
	{{0, -1}, {0, 2}},
	// Monkey
	{{-1, -1}, {-1, 1}, {1, -1}, {1, 1}},
	// Crab
	{{-2, 0}, {0, 1}, {2, 0}},
	// Crane
	{{-1, -1}, {0, 1}, {1, -1}},
	// Frog
	{{1, -1}, {-1, 1}, {-2, 0}},
	// Boar
	{{-1, 0}, {0, 1}, {1, 0}},
	// Horse
	{{-1, 0}, {0, 1}, {0, -1}},
	// Elephant
	{{-1, 1}, {-1, 0}, {1, 0}, {1, 1}},
	// Ox
	{{1, 0}, {0, 1}, {0, -1}},
	// Goose
	{{-1, 1}, {-1, 0}, {1, 0}, {1, -1}},
	// Dragon
	{{-2, 1}, {-1, -1}, {1, -1}, {2, 1}},
	// Mantis
	{{-1, 1}, {0, -1}, {1, 1}},
	// Eel
	{{-1, 1}, {-1, -1}, {1, 0}},
};

std::vector<std::string> card_names {
	"rabbit",
	"cobra",
	"rooster",
	"tiger",
	"monkey",
	"crab",
	"crane",
	"frog",
	"boar",
	"horse",
	"elephant",
	"ox",
	"goose",
	"dragon",
	"mantis",
	"eel",
};

std::vector<int> card_score {
	// Rabbit
	3,
	// Cobra
	2,
	// Rooster
	4,
	// Tiger
	7,
	// Monkey
	5,
	// Crab
	6,
	// Crane
	2,
	// Frog
	3,
	// Boar
	5,
	// Horse
	2,
	// Elephant
	5,
	// Ox
	2,
	// Goose
	4,
	// Dragon
	6,
	// Mantis
	3,
	// Eel
	3,
};

Card parse_card_name(std::string card_name) {
	for (int i = 0; i < card_names.size(); i++)
		if (card_names[i] == card_name)
			return i;
	throw std::runtime_error("Bad card name: " + card_name);
}

struct CardDesc {
	int jump_count;
	SquareDelta jumps[4];
};

std::vector<CardDesc> cards;
std::vector<uint8_t> square_is_legal(256);

static void setup_onitama() {
	for (int y = 0; y < 5; y++)
		for (int x = 0; x < 5; x++)
			square_is_legal[offset_to_delta({x, y})] = 1;
	for (auto card : cards_source) {
		CardDesc desc;
		desc.jump_count = card.size();
		int i = 0;
		for (std::pair<int, int> offset : card)
			desc.jumps[i++] = offset_to_delta(offset);
		cards.push_back(desc);
	}
}

/*
-------------------------
32 33 34 35 36 | 37 38 39
24 25 26 27 28 | 29 30 31
16 17 18 19 20 | 21 22 23
 8  9 10 11 12 | 13 14 15
 0  1  2  3  4 |  5  6  7
*/

// A move is encoded as the sum:
//   [8 bits] (which square to move to) +
//   [3 bits] (which piece to pick up) << 8 +
//   [1 bit ] (which card from the hand to use) << 11 +
//   [4 bits] (which card is being used) << 12
typedef uint16_t Move;

constexpr Move BAD_MOVE = -1;

static inline void swap_gate(uint8_t& x, uint8_t& y) {
	if (x > y)
		std::swap(x, y);
}

static inline void length_two_sort(uint8_t* x) {
	swap_gate(x[0], x[1]);
}

static inline void length_four_sort(uint8_t* x) {
	swap_gate(x[0], x[2]);
	swap_gate(x[1], x[3]);
	swap_gate(x[0], x[1]);
	swap_gate(x[2], x[3]);
	swap_gate(x[1], x[2]);
}

constexpr int PRIORITY_COUNT = 5;
Move moves_scratch[PRIORITY_COUNT][MAX_LEGAL_MOVES];

struct OnitamaState {
	Square white_pieces[5];
	Square black_pieces[5];
	Card white_hand[2];
	Card black_hand[2];
	Card swap_card;
	Player turn;

	static OnitamaState starting_state(uint8_t hand_state[5]) {
		OnitamaState result;
		result.white_pieces[0] = offset_to_delta({2, 0});
		result.white_pieces[1] = offset_to_delta({0, 0});
		result.white_pieces[2] = offset_to_delta({1, 0});
		result.white_pieces[3] = offset_to_delta({3, 0});
		result.white_pieces[4] = offset_to_delta({4, 0});
		result.black_pieces[0] = offset_to_delta({2, 4});
		result.black_pieces[1] = offset_to_delta({0, 4});
		result.black_pieces[2] = offset_to_delta({1, 4});
		result.black_pieces[3] = offset_to_delta({3, 4});
		result.black_pieces[4] = offset_to_delta({4, 4});
		result.white_hand[0] = hand_state[0];
		result.white_hand[1] = hand_state[1];
		result.black_hand[0] = hand_state[2];
		result.black_hand[1] = hand_state[3];
		result.swap_card     = hand_state[4];
		result.turn = Player::WHITE;
		return result;
	}

	// Sort pieces for hashing and computing transpositions.
	void canonicalize() {
		length_four_sort(&white_pieces[1]);
		length_four_sort(&black_pieces[1]);
		length_two_sort(&white_hand[0]);
		length_two_sort(&black_hand[0]);
	}

	template <bool only_loud_moves=false>
	int move_gen(Move* move_buffer) const {
		const Square* our_pieces   = turn == Player::WHITE ? white_pieces : black_pieces;
		const Square* their_pieces = turn == Player::WHITE ? black_pieces : white_pieces;
		const Card* our_hand = turn == Player::WHITE ? white_hand : black_hand;
//		int gen_count = 0;
//		Move* next_output = move_buffer;

		int moves_by_priority[PRIORITY_COUNT]{};

//		int quiet_moves = 0;
//		Move quiet_move_scratch[MAX_LEGAL_MOVES];

		Card sorted_hand[2] = {our_hand[0], our_hand[1]};
		int did_swap = 0;
		if (card_score[sorted_hand[0]] < card_score[sorted_hand[1]]) {
			std::swap(sorted_hand[0], sorted_hand[1]);
			did_swap = 1;
		}

		// Try all of our pieces.
//		for (int piece_index = 0; piece_index < 5; piece_index++) {
		for (int piece_index : {1, 2, 3, 4, 0}) {
			if (our_pieces[piece_index] == PIECE_CAPTURED)
				continue;
			// Try both cards.
			for (int hand_index = 0; hand_index < 2; hand_index++) {
				Card card = sorted_hand[hand_index];
				const CardDesc& card_desc = cards[card];
				// Try all jumps for this card.
				for (int jump_index = 0; jump_index < card_desc.jump_count; jump_index++) {
					SquareDelta original_jump = card_desc.jumps[jump_index];
					SquareDelta jump = turn == Player::WHITE ? original_jump : -original_jump;
					int dest = jump + int(our_pieces[piece_index]);
					if (dest < 0)
						continue;
					if (not square_is_legal[dest])
						continue;
					if (dest == our_pieces[0] or dest == our_pieces[1] or dest == our_pieces[2] or dest == our_pieces[3] or dest == our_pieces[4])
						continue;
					// All captures or winning moves are loud.
					bool is_winning = dest == their_pieces[0] or (piece_index == 0 and (
						(turn == Player::WHITE and dest == offset_to_delta({2, 4})) or
						(turn == Player::BLACK and dest == offset_to_delta({2, 0}))
					));
					bool is_loud_move = is_winning or dest == their_pieces[1] or dest == their_pieces[2] or dest == their_pieces[3] or dest == their_pieces[4];

					if (only_loud_moves and not is_loud_move)
						continue;

					// Determine if we move a king Moore-adjacent to an enemy temple.
					bool temple_threatening = piece_index == 0 and (
						(turn == Player::WHITE and (
//							dest == offset_to_delta({1, 4}) or
							dest == offset_to_delta({1, 3}) or
							dest == offset_to_delta({2, 3}) or
							dest == offset_to_delta({3, 3}) or
//							dest == offset_to_delta({3, 4}) or
							false
						)) or
						(turn == Player::BLACK and (
//							dest == offset_to_delta({1, 0}) or
							dest == offset_to_delta({1, 1}) or
							dest == offset_to_delta({2, 1}) or
							dest == offset_to_delta({3, 1}) or
//							dest == offset_to_delta({3, 0}) or
							false
						))
					);

					assert(dest < 256);
					Move move = dest + (piece_index << 8) + ((did_swap ^ hand_index) << 11); //+ (((Move)card) << 12);
					if (is_winning) {
						moves_scratch[0][moves_by_priority[0]++] = move;
					} else if (is_loud_move) {
						moves_scratch[1][moves_by_priority[1]++] = move;
					} else if (temple_threatening) {
						moves_scratch[2][moves_by_priority[2]++] = move;
					} else {
						// Determine if the move moves forward or not.
						if (original_jump > 3) {
							moves_scratch[3][moves_by_priority[3]++] = move;
						} else {
							moves_scratch[4][moves_by_priority[4]++] = move;
						}
					}
				}
			}
		}
		int gen_count = 0;
		for (int p = 0; p < PRIORITY_COUNT; p++) {
			for (int i = 0; i < moves_by_priority[p]; i++) {
				*move_buffer++ = moves_scratch[p][i];
				gen_count++;
			}
		}
		assert(gen_count <= MAX_LEGAL_MOVES);
		return gen_count;
	}

	void sanity_check() {
		uint64_t computed_unoccupied = 0;
		for (int y = 0; y < 5; y++)
			for (int x = 0; x < 5; x++)
				computed_unoccupied |= 1ull << offset_to_delta({x, y});
		for (int i = 0; i < 5; i++) {
			uint64_t bit;
			bit = 1ull << white_pieces[i];
			assert(computed_unoccupied & bit);
			computed_unoccupied &= ~bit;
			bit = 1ull << black_pieces[i];
			assert(computed_unoccupied & bit);
			computed_unoccupied &= ~bit;
		}
//		assert(computed_unoccupied == unoccupied_cells);
	}

	void make_move(Move m) {
		Square* our_pieces   = turn == Player::WHITE ? white_pieces : black_pieces;
		Square* their_pieces = turn == Player::WHITE ? black_pieces : white_pieces;
		Card* our_hand = turn == Player::WHITE ? white_hand : black_hand;
		Square dest = m;
		int piece_index = (m >> 8) & 7;
		int hand_index = (m >> 11) & 1;
		Square source = our_pieces[piece_index];
//		unoccupied_cells |= 1ull << source;
//		unoccupied_cells &= ~(1ull << dest);
		our_pieces[piece_index] = dest;
		// Evaluate captures.
		for (int i = 0; i < 5; i++)
			if (their_pieces[i] == dest)
				their_pieces[i] = PIECE_CAPTURED;
		// Change cards in hands.
		std::swap(our_hand[hand_index], swap_card);
		turn = static_cast<Player>(1 - turn);
		canonicalize();
//		sanity_check();
	}

	Player game_result() const {
		if (white_pieces[0] == PIECE_CAPTURED)
			return Player::BLACK;
		if (black_pieces[0] == PIECE_CAPTURED)
			return Player::WHITE;
		if (white_pieces[0] == offset_to_delta({2, 4}))
			return Player::WHITE;
		if (black_pieces[0] == offset_to_delta({2, 0}))
			return Player::BLACK;
		return Player::NOBODY;
	}
};

#if 0

int default_king_score_table[40] = {
	 0,  1,  2,  1,  0,   0, 0, 0,
	 1,  2,  3,  2,  1,   0, 0, 0,
	 5, 12, 15, 12,  5,   0, 0, 0,
	 5, 20, 30, 20,  5,   0, 0, 0,
	-5, 10,  0, 10, -5,   0, 0, 0,
};

int default_pawn_score_table[40] = {
	  0,   1,  2,  1,   0,   0, 0, 0,
	  1,   2,  3,  2,   1,   0, 0, 0,
	  2,   7, 10,  7,   2,   0, 0, 0,
	  0,   2,  3,  2,   0,   0, 0, 0,
	-10,  -5, -3, -5, -10,   0, 0, 0,
};

#else

std::vector<int> default_king_score_table{
	-35, -19,  13, -19, -35,   0, 0, 0,
	-21,  -9,  -2,  -9, -21,   0, 0, 0,
	-18,  36,  59,  36, -18,   0, 0, 0,
	 70, 147, 147, 147,  70,   0, 0, 0,
	107, 167, 200, 167, 107,   0, 0, 0,
};

std::vector<int> default_pawn_score_table{
	-5,  -2, -13,  -2,  -5,   0, 0, 0,
	 2,  14,  15,  14,   2,   0, 0, 0,
	17,  44,  68,  44,  17,   0, 0, 0,
	31,  75, 104,  75,  31,   0, 0, 0,
	26,  48,  58,  48,  26,   0, 0, 0,
};

#endif

uint64_t state_to_hash(const OnitamaState& state) {
	const uint64_t* as_blocks = reinterpret_cast<const uint64_t*>(&state);
	return as_blocks[0] + as_blocks[1]  * 7;
}

int make_mate_scores_slightly_less_extreme(int score) {
	if (score < -10000)
		return score + 1;
	if (score > 10000)
		return score - 1;
	return score;
}

struct OnitamaEngine {
	std::unordered_map<uint64_t, Move> move_order_table;
	uint64_t nodes_reached = 0;
	int play_randomization = 0;
	std::vector<int> king_score_table = default_king_score_table;
	std::vector<int> pawn_score_table = default_pawn_score_table;
	std::vector<Move> killer_moves{std::vector<Move>(100, BAD_MOVE)};
	std::atomic<bool> time_limit_up;

	int heuristic_score(const OnitamaState& state) {
		// Get one point for each.
		Player result = state.game_result();
		if (result != Player::NOBODY)
			return result == state.turn ? 99999 : -99999;

		// Tempo bonus.
		int score_for_white = state.turn == Player::WHITE ? 15 : -15;
		score_for_white += king_score_table[state.white_pieces[0]] / 2;
		score_for_white -= king_score_table[36 - state.black_pieces[0]] / 2;
		for (int i = 1; i < 5; i++) {
			if (state.white_pieces[i] == PIECE_CAPTURED)
				score_for_white -= 100;
			else
				score_for_white += pawn_score_table[state.white_pieces[i]] / 2;
			if (state.black_pieces[i] == PIECE_CAPTURED)
				score_for_white += 100;
			else
				score_for_white -= pawn_score_table[36 - state.black_pieces[i]] / 2;
		}
		return state.turn == Player::WHITE ? score_for_white : -score_for_white;
	}

	template <bool quiescence=false>
	int pvs(const OnitamaState& state, int depth, int alpha, int beta, Move* best_move_seen_ptr=nullptr) {
		if (time_limit_up)
			return 123456789;
		nodes_reached++;
		Player result = state.game_result();
		if (depth == 0 or result != Player::NOBODY) {
			if (quiescence or (result != Player::NOBODY))
				return heuristic_score(state);
			return pvs<true>(state, 10, alpha, beta);
		}

		constexpr int MAX_PADDING = 2;
		Move raw_moves[MAX_LEGAL_MOVES + MAX_PADDING];
		Move* moves = raw_moves + MAX_PADDING;

		int move_count = state.move_gen<quiescence>(moves);
		if (quiescence and move_count == 0)
			return heuristic_score(state);
		assert(move_count > 0);

		auto promote_move = [&moves, &move_count](Move m) {
			// Move this move to the front of the queue.
			for (int i = 0; i < move_count; i++) {
				if (moves[i] == m) {
					moves[i] = BAD_MOVE;
					moves--;
					moves[0] = m;
					move_count++;
					break;
				}
			}
		};

#ifdef USE_KILLER
		// If we have a killer move order that front.
		if ((not quiescence) and killer_moves[depth] != BAD_MOVE)
			promote_move(killer_moves[depth]);
#endif

#ifdef USE_TABLE
		uint64_t state_hash;
		// Reorder our moves according to our table.
		if (not quiescence) {
			state_hash = state_to_hash(state);
			auto it = move_order_table.find(state_hash);
			if (it != move_order_table.end())
				promote_move(it->second);
		}
#endif

		int best_score_seen = -SCORE_INF;
		Move best_move_seen = BAD_MOVE;

		for (int i = 0; i < move_count; i++) {
			// Skip sentinels.
			if (moves[i] == BAD_MOVE)
				continue;
			OnitamaState child_state = state;
			child_state.make_move(moves[i]);

			int score;
			if (i == 0) {
				score = -pvs<quiescence>(child_state, depth - 1, -beta, - alpha);
			} else {
				score = -pvs<quiescence>(child_state, depth - 1, -alpha - 1, -alpha);
				if (alpha < score and score < beta)
					score = -pvs<quiescence>(child_state, depth - 1, -beta, -score);
			}
			if (score > best_score_seen) {
				best_score_seen = score;
				best_move_seen = moves[i];
			}
#ifdef USE_TABLE
			if (score > alpha and (not quiescence) and (not time_limit_up))
				move_order_table[state_hash] = moves[i];
#endif
			alpha = std::max(alpha, score);
			if (alpha >= beta) {
#ifdef USE_KILLER
				if ((not quiescence) and (not time_limit_up))
					killer_moves[depth] = moves[i];
#endif
				break;
			}
		}
		if (best_move_seen_ptr != nullptr)
			*best_move_seen_ptr = best_move_seen;
		return make_mate_scores_slightly_less_extreme(alpha);
	}

	static void set_limit_up(double time_limit_seconds, OnitamaEngine* self) {
		std::this_thread::sleep_for(std::chrono::duration<double>(time_limit_seconds));
		self->time_limit_up = true;
	}

	Move compute_best_move(const OnitamaState& state, int depth, double time_limit_seconds=-1) {
		time_limit_up = false;
		std::unique_ptr<std::thread> t;
		if (time_limit_seconds != -1)
			t = std::make_unique<std::thread>(OnitamaEngine::set_limit_up, time_limit_seconds, this);

//		Move moves[MAX_LEGAL_MOVES];
//		int move_count = state.move_gen(moves);
//		int best_score_so_far = -SCORE_INF;
		Move best_move = BAD_MOVE;

		// Iteratively deepen.
		for (int i_depth = 1; i_depth <= depth; i_depth++) {
			/*
			for (int i = 0; i < move_count; i++) {
				if (time_limit_up && i_depth != 1)
					break;
				OnitamaState child_state = state;
				child_state.make_move(moves[i]);
				int score = -pvs(child_state, i_depth, -SCORE_INF, SCORE_INF);
				// Need to check again, because pvs' result might be invalid.
				if (time_limit_up)
					break;
				score += std::uniform_int_distribution<int>(0, play_randomization)(rng);
				if (score > best_score_so_far) {
					best_score_so_far = score;
					best_move = moves[i];
				}
			}*/
			int score = pvs(state, i_depth, -SCORE_INF, SCORE_INF, &best_move);
			if (time_limit_up)
				break;
			std::cout << "info depth " << i_depth << " nodes " << nodes_reached << " score " << score << std::endl;
		}

		if (t != nullptr)
			t->join();

		return best_move;
	}
};

std::vector<std::string> piece_strings {
	".",
	"\033[91mK\033[0m",
	"\033[94mK\033[0m",

	"\033[91mp\033[0m",
	"\033[91mp\033[0m",
	"\033[91mp\033[0m",
	"\033[91mp\033[0m",

	"\033[94mp\033[0m",
	"\033[94mp\033[0m",
	"\033[94mp\033[0m",
	"\033[94mp\033[0m",
};

std::string player_to_string(Player player) {
	if (player == Player::WHITE)
		return "\033[91mRED\033[0m";
	if (player == Player::BLACK)
		return "\033[94mBLUE\033[0m";
	if (player == Player::NOBODY)
		return "NOBODY";
	return "???BUG???";
}

std::string move_to_string(Move m) {
	Square dest = m;
	int x = dest % 8;
	int y = dest / 8;
	int piece = (m >> 8) & 7;
	int hand_index = (m >> 11) & 1;
	return std::to_string(x) + "," + std::to_string(y) + "p" + std::to_string(piece) + "h" + std::to_string(hand_index);
}

void print_state(const OnitamaState& state) {
	std::cout << "Turn: " << player_to_string(state.turn) << std::endl;
	int contains[40]{};
	if (state.white_pieces[0] != PIECE_CAPTURED) {
		contains[state.white_pieces[0]] = 1;
	}
	if (state.black_pieces[0] != PIECE_CAPTURED) {
		assert(contains[state.black_pieces[0]] == 0);
		contains[state.black_pieces[0]] = 2;
	}
	for (int i = 1; i < 5; i++) {
		if (state.white_pieces[i] != PIECE_CAPTURED) {
			assert(contains[state.white_pieces[i]] == 0);
			contains[state.white_pieces[i]] = 2 + i; //3;
		}
		if (state.black_pieces[i] != PIECE_CAPTURED) {
			assert(contains[state.black_pieces[i]] == 0);
			contains[state.black_pieces[i]] = 6 + i; //4;
		}
	}
	for (int y = 0; y < 5; y++) {
		for (int x = 0; x < 5; x++) {
			if (x != 0)
				std::cout << " ";
			SquareDelta delta = offset_to_delta({x, 4 - y});
//			std::cout << bool(state.unoccupied_cells & (1ull << delta));
			std::cout << piece_strings[contains[delta]];
		}
		std::cout << std::endl;
	}
	std::cout << "Hands: \033[91m[" << card_names[state.white_hand[0]] << " " << card_names[state.white_hand[1]] << "]\033[0m \033[94m[";
	std::cout << card_names[state.black_hand[0]] << " " << card_names[state.black_hand[1]] << "]\033[0m " << card_names[state.swap_card] << std::endl;

}

// ===== Interface =====

void play_interface(OnitamaState state) {
	OnitamaEngine engine;
	while (state.game_result() == Player::NOBODY) {
		std::cout << std::endl;
		print_state(state);
		Move found_move = BAD_MOVE;
		while (found_move == BAD_MOVE) {
			std::cout << "Input move as: hand_index source_x source_y dest_x dest_y: " << std::endl;
			int hand_index, source_x, source_y, dest_x, dest_y;
			std::cin >> hand_index;
			std::cin >> source_x;
			std::cin >> source_y;
			std::cin >> dest_x;
			std::cin >> dest_y;
			Move moves[MAX_LEGAL_MOVES];
			int move_count = state.move_gen(moves);
			for (int i = 0; i < move_count; i++) {
				std::cout << " " << move_to_string(moves[i]);
				Move m = moves[i];
				Square dest = m;
				int piece_index = (m >> 8) & 7;
				int m_hand_index = (m >> 11) & 1;
				const Square* our_pieces = state.turn == Player::WHITE ? state.white_pieces : state.black_pieces;
				Square source = our_pieces[piece_index];
				if (source == offset_to_delta({source_x, source_y}) and dest == offset_to_delta({dest_x, dest_y}) and hand_index == m_hand_index)
					found_move = m;
			}
			std::cout << std::endl;
		}
		state.make_move(found_move);
		std::cout << std::endl;
		print_state(state);
		if (state.game_result() != Player::NOBODY)
			break;
		Move opponent_move = engine.compute_best_move(state, 9);
		state.make_move(opponent_move);
	}
	std::cout << "Winner: " << player_to_string(state.game_result()) << std::endl;
}

int calibration_self_play_game(OnitamaEngine& engine, std::vector<int>& king_wins, std::vector<int>& king_losses, std::vector<int>& pawn_wins, std::vector<int>& pawn_losses) {
	Card hand_state[16];
	for (int i = 0; i < 16; i++)
		hand_state[i] = i;
	std::shuffle(&hand_state[0], &hand_state[16], rng);
	auto state = OnitamaState::starting_state(hand_state);
	int plies = 0;
	std::vector<int> white_king_occurences(40), black_king_occurences(40), white_pawn_occurences(40), black_pawn_occurences(40);
	auto track_piece = [](Square location, std::vector<int>& dest) {
		if (location == PIECE_CAPTURED)
			return;
		dest.at(location)++;
	};
	while (state.game_result() == Player::NOBODY) {
		Move m = engine.compute_best_move(state, 3);
		state.make_move(m);
		track_piece(state.white_pieces[0], white_king_occurences);
		track_piece(state.black_pieces[0], black_king_occurences);
		for (int i = 1; i < 5; i++) {
			track_piece(state.white_pieces[i], white_pawn_occurences);
			track_piece(state.black_pieces[i], black_pawn_occurences);
		}
		plies++;
		if (plies >= 100)
			break;
	}
	if (state.game_result() == Player::NOBODY)
		return plies;
	for (int i = 0; i < 40; i++) {
		if (state.game_result() == Player::WHITE) {
			king_wins[i]        += white_king_occurences[i];
			king_losses[36 - i] += black_king_occurences[i];
			pawn_wins[i]        += white_pawn_occurences[i];
			pawn_losses[36 - i] += black_pawn_occurences[i];
		} else {
			king_losses[i]    += white_king_occurences[i];
			king_wins[36 - i] += black_king_occurences[i];
			pawn_losses[i]    += white_pawn_occurences[i];
			pawn_wins[36 - i] += black_pawn_occurences[i];
		}
	}
	return plies;
}

void do_self_play_piece_table_calibration() {
	std::vector<int> king_wins(40), king_losses(40);
	std::vector<int> pawn_wins(40), pawn_losses(40);

	OnitamaEngine engine;
	engine.play_randomization = 20;

	for (int i = 0; i < 100; i++) {
		int plies = calibration_self_play_game(engine, king_wins, king_losses, pawn_wins, pawn_losses);
		if (i % 10 == 0)
			std::cout << "[" << i << "] Generated game of: " << plies << std::endl;
	}

	int total = 0;
	for (int i = 0; i < 40; i++)
		total += king_wins[i] + king_losses[i] + pawn_wins[i] + pawn_losses[i];

	std::cout << "Tables:" << std::endl;
	auto print_table = [](const std::vector<int>& array) {
		std::cout << "[";
		int index = 0;
		for (int x : array) {
			if (index != 0)
				std::cout << ", ";
			std::cout << x;
			if (index % 8 == 7)
				std::cout << std::endl;
			index++;
		}
		std::cout << "]";
	};
	std::cout << "king_wins   = "; print_table(king_wins); std::cout << std::endl;
	std::cout << "king_losses = "; print_table(king_losses); std::cout << std::endl;
	std::cout << "pawn_wins   = "; print_table(pawn_wins); std::cout << std::endl;
	std::cout << "pawn_losses = "; print_table(pawn_losses); std::cout << std::endl;

	std::vector<int> king_value(40);
	std::vector<int> pawn_value(40);
	for (int i = 0; i < 40; i++) {
		if (king_wins[i] + king_losses[i] == 0)
			continue;
		double proportion = king_wins[i] / (double)(king_wins[i] + king_losses[i]);
		king_value[i] = 100000 * (proportion - 0.5);
	}
	for (int i = 0; i < 40; i++) {
		if (pawn_wins[i] + pawn_losses[i] == 0)
			continue;
		double proportion = pawn_wins[i] / (double)(pawn_wins[i] + pawn_losses[i]);
		pawn_value[i] = 100000 * (proportion - 0.5);
	}
	std::cout << "king_table = "; print_table(king_value); std::cout << std::endl;
	std::cout << "pawn_table = "; print_table(pawn_value); std::cout << std::endl;

	// Average over reflections.
	for (int y = 0; y < 5; y++) {
		for (int x = 0; x < 3; x++) {
			king_value[offset_to_delta({x, y})] += king_value[offset_to_delta({4 - x, y})];
			king_value[offset_to_delta({4 - x, y})] = king_value[offset_to_delta({x, y})];
			pawn_value[offset_to_delta({x, y})] += pawn_value[offset_to_delta({4 - x, y})];
			pawn_value[offset_to_delta({4 - x, y})] = pawn_value[offset_to_delta({x, y})];
		}
	}

	for (int i = 0; i < 40; i++) {
		king_value[i] /= 1000;
		pawn_value[i] /= 1000;
	}

	std::cout << "Symmetrized:" << std::endl;
	std::cout << "king_table = "; print_table(king_value); std::cout << std::endl;
	std::cout << "pawn_table = "; print_table(pawn_value); std::cout << std::endl;
	std::cout << "Nodes explored: " << engine.nodes_reached << std::endl;
	std::cout << "Table size: " << engine.move_order_table.size() << std::endl;
}

// ===== Elo tournament =====

Player elo_self_play_game(OnitamaEngine& engine1, OnitamaEngine& engine2) {
	Card hand_state[16];
	for (int i = 0; i < 16; i++)
		hand_state[i] = i;
	std::shuffle(&hand_state[0], &hand_state[16], rng);
	auto state = OnitamaState::starting_state(hand_state);
	int plies = 0;
	while (state.game_result() == Player::NOBODY) {
		Move m = (plies % 2 == 0 ? engine1 : engine2).compute_best_move(state, 3);
		state.make_move(m);
		plies++;
		if (plies >= 100)
			break;
	}
	return state.game_result();
}

void do_elo_testing() {
	auto fill_with_scale = [](OnitamaEngine& engine, double scale) {
		for (int i = 0; i < 40; i++) {
			engine.king_score_table[i] = scale * default_king_score_table[i];
			engine.pawn_score_table[i] = scale * default_pawn_score_table[i];
		}
	};

	OnitamaEngine engine1;
	OnitamaEngine engine2;
	engine1.play_randomization = 10;
	engine2.play_randomization = 10;
	fill_with_scale(engine1, 1.0);
	fill_with_scale(engine2, -1.0);

	int wins[2]{};
	for (int i = 0; i < 100; i++) {
		Player result = elo_self_play_game(engine1, engine2);
		if (result == Player::WHITE)
			wins[0]++;
		if (result == Player::BLACK)
			wins[1]++;
		result = elo_self_play_game(engine2, engine1);
		if (result == Player::WHITE)
			wins[1]++;
		if (result == Player::BLACK)
			wins[0]++;
		std::cout << "Score: " << wins[0] << " - " << wins[1] << std::endl;
	}

}

void uoi() {
	OnitamaEngine engine;
	OnitamaState state;
	auto get_card = []() {
		std::string card_name;
		std::cin >> card_name;
		return parse_card_name(card_name);
	};
	auto get_int = []() {
		int i;
		std::cin >> i;
		return i;
	};
	while (true) {
		std::string cmd;
		std::cin >> cmd;
		if (cmd == "newgame") {
			Card hand_state[5]{};
			for (int i = 0; i < 5; i++)
				hand_state[i] = get_card();
			state = OnitamaState::starting_state(hand_state);
			std::cout << "info new game." << std::endl;
//			print_state(state);
		}
		if (cmd == "move") {
			Card c = get_card();
			// Find the card that matches in our hand.
			const Card* our_hand = state.turn == Player::WHITE ? state.white_hand : state.black_hand;
			int hand_index;
			if (our_hand[0] == c) {
				hand_index = 0;
			} else if (our_hand[1] == c) {
				hand_index = 1;
			} else {
				assert(false);
			}
			int source_x = get_int();
			int source_y = get_int();
			int dest_x = get_int();
			int dest_y = get_int();
			Move moves[MAX_LEGAL_MOVES];
			int move_count = state.move_gen(moves);
			Move found_move = BAD_MOVE;
			for (int i = 0; i < move_count; i++) {
				Move m = moves[i];
				Square dest = m;
				int piece_index = (m >> 8) & 7;
				int m_hand_index = (m >> 11) & 1;
				const Square* our_pieces = state.turn == Player::WHITE ? state.white_pieces : state.black_pieces;
				Square source = our_pieces[piece_index];
				if (source == offset_to_delta({source_x, source_y}) and dest == offset_to_delta({dest_x, dest_y}) and hand_index == m_hand_index)
					found_move = m;
			}
			assert(found_move != BAD_MOVE);
			state.make_move(found_move);
			std::cout << "info Making move: " << found_move << std::endl;
//			print_state(state);
		}
		if (cmd == "genmove") {
			int ms = get_int();
//			std::cout << "info Thinking for: " << ms << std::endl;
			// Apply a little bit of safety.
			ms = std::min(ms, std::max(5, ms - 10));
			// Adjudicate the game.
			if (state.game_result() != Player::NOBODY) {
				if (state.game_result() == state.turn)
					std::cout << "bestmove win" << std::endl;
				else
					std::cout << "bestmove loss" << std::endl;
				continue;
			}
			Move m = engine.compute_best_move(state, 100, ms * 1e-3);
			Square dest = m;
			int piece_index = (m >> 8) & 7;
			const Square* our_pieces = state.turn == Player::WHITE ? state.white_pieces : state.black_pieces;
			Square source = our_pieces[piece_index];
			int m_hand_index = (m >> 11) & 1;
			const Card* our_hand = state.turn == Player::WHITE ? state.white_hand : state.black_hand;
//			std::cout << "Our move: " << m << std::endl;
//			std::cout << "Piece: " << piece_index << std::endl;
			std::cout << "bestmove " << card_names[our_hand[m_hand_index]] << " " << (source % 8) << " " << (source / 8) << " " << (dest % 8) << " " << (dest / 8) << std::endl;
//			state.make_move(m);
//			print_state(state);
		}
		if (cmd == "quit") {
			return;
		}
	}
}

int main() {
	setup_onitama();

	uoi();
	return 0;

//	do_elo_testing();
//	do_self_play_piece_table_calibration();
//	return 0;

//	Move moves[MAX_LEGAL_MOVES];
//	Card hand_state[16];
//	for (int i = 0; i < 16; i++)
//		hand_state[i] = i;
//	std::shuffle(&hand_state[0], &hand_state[16], rng);
	uint8_t hand_state[] = {0, 1, 13, 3, 4};
//	uint8_t hand_state[] = {2, 3, 0, 1, 4};
	auto state = OnitamaState::starting_state(hand_state);

//	std::cout << "Root score: " << negamax(state, 11) << std::endl;

	OnitamaEngine engine;

	for (int depth = 1; depth <= 1000; depth++) {
//		int score = engine.pvs(state, depth, -SCORE_INF, SCORE_INF);
		int score = engine.pvs(state, depth, -SCORE_INF, SCORE_INF);
		std::cout << "Depth: " << depth << " Root score: " << score << std::endl;
	}
	std::cout << "Nodes explored: " << engine.nodes_reached << std::endl;
	std::cout << "Table size: " << engine.move_order_table.size() << std::endl;

	return 0;

//	play_interface(state);
//	return 0;

//	Move m = compute_best_move(state, 7);
//	int score = negamax(state, 8);
//	std::cout << "Computed: " << score << std::endl;
//	state.sanity_check();
//	negamax(state, 2);

/*
	for (int depth = 0; depth < 15; depth++) {
		int nodes = perft(state, depth);
		int nm_score = negamax(state, depth);
		std::cout << "Depth: " << depth << " Nodes: " << nodes << " Score: " << nm_score << std::endl;
	}

	std::cout << "Total nodes: " << nodes_reached << std::endl;
*/
}

