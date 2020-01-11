#!/usr/bin/python

import subprocess, datetime, time, argparse, random

cards = """
rabbit
cobra
rooster
tiger
monkey
crab
crane
frog
boar
horse
elephant
ox
goose
dragon
mantis
eel
""".strip().split()

class Player:
	def __init__(self, cmd):
		self.cmd = cmd
		self.proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE)

	def send(self, s):
		self.proc.stdin.write(s.encode("ascii"))
		self.proc.stdin.flush()

	def new_game(self, hand_cards):
		self.send("newgame %s %s %s %s %s\n" % tuple(hand_cards))

	def move(self, move):
		self.send("move %s\n" % (move,))

	def genmove(self, seconds):
		self.send("genmove %i\n" % (int(1e3 * seconds),))
		start_time = time.time()
		while True:
			line = self.proc.stdout.readline().strip().decode("utf8")
			if not line:
				raise ValueError("Empty read")
			if line.startswith("info "):
				print(line)
			if line.startswith("bestmove "):
				elapsed = time.time() - start_time
				if elapsed > seconds:
					print("WARNING:", self.cmd, "used", elapsed, "when it was only allotted", seconds)
				return line.split(" ", 1)[1]

	def quit(self):
		self.send("quit\n")
		time.sleep(0.1)
		try:
			self.proc.kill()
		except OSError:
			pass
		self.proc.wait()

def write_game(args, p1_name, p2_name, opening, moves, outcome):
	with open(args.pgn_out, "a+") as f:
		print('[Event "?"]', file=f)
		print('[Site "?"]', file=f)
		print('[Date "%s"]' % (datetime.datetime.now().strftime("%Y.%m.%d"),), file=f)
		print('[Round "?"]', file=f)
		print('[White "%s"]' % (p1_name,), file=f)
		print('[Black "%s"]' % (p2_name,), file=f)
		print('[Opening "%s"]' % " ".join(opening,), file=f)
		print('[Plycount "%i"]' % (len(moves),), file=f)
		print('[Result "%s"]' % (outcome,), file=f)
		print('[TimeControl "+%r"]' % (args.tc,), file=f)
		print(file=f)
		print(" ".join(moves), file=f)
		print(file=f)

parser = argparse.ArgumentParser()
parser.add_argument("--engine", metavar="CMD", action="append", help="Engine command.")
parser.add_argument("--pgn-out", metavar="PATH", type=str, default=None, help="PGN path to output games to.")
parser.add_argument("--tc", metavar="SEC", type=float, default=1.0, help="Seconds per move.")

if __name__ == "__main__":
	args = parser.parse_args()
	assert len(args.engine) == 2
	engine_commands = list(args.engine)

	while True:
		print("Playing %s - %s" % tuple(engine_commands))
		players = list(map(Player, engine_commands))

		opening = random.sample(cards, 5)
		for player in players:
			player.new_game(opening)

		flipped = False
		all_moves = []
		while True:
			move = players[0].genmove(args.tc)
			print("Got move:", move)
			if move in ("win", "draw", "loss"):
				# Make sure the other player agrees.
				second_opinion = players[1].genmove(0.1)
				assert move == second_opinion, "Disagree about state: %r != %r" % (move, second_opinion)
				break
			all_moves.append(move)
			for player in players:
				player.move(move)
			players.reverse()
			flipped = not flipped

		for player in players:
			player.quit()

		if args.pgn_out is not None:
			write_game(
				args,
				p1_name=engine_commands[0],
				p2_name=engine_commands[1],
				opening=opening,
				moves=all_moves,
				outcome={
					(False, "win"):  "1-0",
					(True,  "win"):  "0-1",
					(False, "loss"): "0-1",
					(True,  "loss"): "1-0",
				}[flipped, move] if move != "draw" else "1/2-1/2",
			)

		engine_commands.reverse()

