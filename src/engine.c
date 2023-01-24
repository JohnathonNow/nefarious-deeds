#include <minilang/ml_library.h>
#include <minilang/ml_macros.h>
#include <minilang/ml_object.h>
#include <minilang/ml_polynomial.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <complex.h>
#undef I

typedef struct event_t event_t;
typedef struct player_t player_t;
typedef struct game_t game_t;

typedef enum {
	ACTION_NONE,
	ACTION_MOVE,
	ACTION_KICK
} action_t;

typedef double double2 __attribute__((vector_size(16)));

static inline double dot(double2 A, double2 B) {
	return A[0] * B[0] + A[1] * B[1];
}

struct event_t {
	event_t *Next;
	double2 Target;
	double Time;
	int Player;
	action_t Action;
};

typedef struct {
	double2 Position, Velocity, Target;
	double Angle, Rotation, TargetAngle;
	double Tackled;
} player_state_t;

typedef struct {
	double Time;
	struct {
		double2 Position, Velocity, Friction;
		double Spawn;
		int Handler;
	} Ball;
	int Score[2];
	player_state_t Players[];
} game_state_t;

struct game_t {
	ml_type_t *Type;
	//FILE *Log;
	player_t *Players;
	event_t *Events;
	game_state_t *Base, *State;
	int NumPlayers, StateSize;
	int TeamSize[2];
};

struct player_t {
	ml_type_t *Type;
	player_t *Next;
	game_t *Game;
	int Index, Team;
};

#define PITCH_WIDTH 180.0
#define PITCH_HEIGHT 100.0
#define GOAL_SIZE 20.0

#define BALL_RADIUS 1.0
#define PLAYER_RADIUS 3.0

#define BALL_FRICTION 5.0
#define BALL_KICK_SPEED 30.0

#define PLAYER_RUN_SPEED 20.0
#define PLAYER_DRIBBLE_SPEED 10.0

game_t *game() {
	game_t *Game = new(game_t);
	return Game;
}

player_t *game_player(game_t *Game, int Team) {
	player_t *Player = new(player_t);
	Player->Game = Game;
	Player->Team = Team;
	Player->Next = Game->Players;
	Game->Players = Player;
	return Player;
}

void game_start(game_t *Game) {
	srand(time(0));
	int *TeamSize = Game->TeamSize;
	TeamSize[0] = TeamSize[1] = 0;
	for (player_t *Player = Game->Players; Player; Player = Player->Next) {
		Player->Index = TeamSize[Player->Team]++;
	}
	for (player_t *Player = Game->Players; Player; Player = Player->Next) {
		if (Player->Team) Player->Index += TeamSize[0];
	}
	int NumPlayers = Game->NumPlayers = TeamSize[0] + TeamSize[1];
	int StateSize = Game->StateSize = sizeof(game_state_t) + NumPlayers * sizeof(player_state_t);
	game_state_t *State = Game->Base = (game_state_t *)snew(StateSize);
	player_state_t *PlayerState = State->Players;
	for (int I = NumPlayers; --I >= 0; ++PlayerState) {
		double2 Position = {
			0.9 * PITCH_WIDTH * ((double)rand() / RAND_MAX - 0.5),
			0.9 * PITCH_HEIGHT * ((double)rand() / RAND_MAX - 0.5)
		};
		PlayerState->Position = PlayerState->Target = Position;
		PlayerState->Velocity = (double2){0, 0};
	}
	State->Ball.Spawn = 1.0;
	State->Ball.Position = (double2){0, 0};
	State->Ball.Velocity = (double2){0, 0};
	State->Ball.Friction = (double2){0, 0};
	//State->Ball.Velocity = (double2){17, 15};
	//double2 Direction = State->Ball.Velocity / sqrt(dot(State->Ball.Velocity, State->Ball.Velocity));
	//State->Ball.Friction = Direction * BALL_FRICTION;
	State->Ball.Handler = -1;
	State->Score[0] = State->Score[1] = 0;
	State->Time = 0;
	Game->State = (game_state_t *)snew(StateSize);
	//Game->Log = fopen("log.csv", "w");
	//fprintf(Game->Log, "time,delta,update,px,py,vx,vy,fx,fy\n");
}

static event_t *EventCache = NULL;

void player_event(player_t *Player, double Time, double2 Target, action_t Action) {
	game_t *Game = Player->Game;
	// Add a player action (movement and/or kick) to game events.
	// If time is older than last base game state, action is discarded.
	if (Time <= Game->Base->Time) return;
	//if (Time > Game->MaxTime) return;
	event_t *Event = EventCache;
	if (!Event) {
		Event = new(event_t);
	} else {
		EventCache = Event->Next;
	}
	Event->Player = Player->Index;
	Event->Time = Time;
	Event->Action = Action;
	Event->Target = Target;
	event_t **Slot = &Game->Events;
	while (Slot[0] && Slot[0]->Time < Time) Slot = &Slot[0]->Next;
	Event->Next = Slot[0];
	Slot[0] = Event;
}

static double solve_quadratic(double P0, double P1, double P2) {
	//printf("Solving %g*t^2 + %g*t + %g = 0\n", P2, P1, P0);
	double D = P1 * P1 - 4 * P2 * P0;
	if (D < 0) return INFINITY;
	D = sqrt(D);
	if (P2 < 0) {
		double T = (-P1 + D) / (2 * P2);
		//printf("\tt = %g\n", T);
		if (T >= 0) return T;
		T = (-P1 - D) / (2 * P2);
		//printf("\tt = %g\n", T);
		if (T >= 0) return T;
	} else {
		double T = (-P1 - D) / (2 * P2);
		//printf("\tt = %g\n", T);
		if (T >= 0) return T;
		T = (-P1 + D) / (2 * P2);
		//printf("\tt = %g\n", T);
		if (T >= 0) return T;
	}
	return INFINITY;
}

static double solve_quartic(double P0, double P1, double P2, double P3, double P4) {
	//printf("Solving %g*t^4 + %g*t^3 + %g*t^2 + %g*t + %g\n", P4, P3, P2, P1, P0);
	complex double Coeffs[] = {P0, P1, P2, P3, P4};
	complex double Roots[4];
	ml_roots_quartic(Coeffs, Roots);
	double T = INFINITY;
	for (int I = 0; I < 4; ++I) {
		//printf("\tt = %g + %gi\n", creal(Z[I]), cimag(Z[I]));
		if (fabs(cimag(Roots[I])) < 1e-9) {
			double T1 = creal(Roots[I]);
			if (T1 > 1e-12 && T1 < T) T = T1;
		}
	}
	return T;
}

typedef enum {
	UPDATE_NONE,
	UPDATE_BALL_STOP,
	UPDATE_BALL_WALL_X,
	UPDATE_BALL_WALL_Y,
	UPDATE_BALL_GOAL,
	UPDATE_BALL_SPAWN,
	UPDATE_PLAYER_BALL,
	UPDATE_PLAYER_STOP_MOVING,
	UPDATE_PLAYER_STOP_TURNING,
	UPDATE_PLAYER_WALL_X,
	UPDATE_PLAYER_WALL_Y,
	UPDATE_PLAYER_TACKLE,
	UPDATE_PLAYER_EVENT
} update_t;

const char *UpdateNames[] = {
	"NONE",
	"BALL_STOP",
	"BALL_WALL_X",
	"BALL_WALL_Y",
	"BALL_GOAL",
	"BALL_PLAYER",
	"PLAYER_STOP_MOVING",
	"PLAYER_STOP_TURNING",
	"PLAYER_WALL_X",
	"PLAYER_WALL_Y",
	"PLAYER_PLAYER",
	"PLAYER_EVENT"
};

void game_predict(game_t *Game, double Target) {
	// Predicts the game state at some time after the current base state, applying known events as necessary.
	game_state_t *State = Game->State;
	memcpy(State, Game->Base, Game->StateSize);
	double Time = State->Time;
	event_t *Event = Game->Events;
	int NumPlayers = Game->NumPlayers;
	for (;;) {
		double Delta = Target - Time;
		update_t Update = UPDATE_NONE;
		int UpdatePlayer;

		// Check for next update
		// 1. Check for ball events (collision / stopping due to friction)
		// 2. Check for player events (collision / reaching target)
		// 3. Check for next player event

		// Check for players stopping due to reaching their target
		player_state_t *PlayerState = State->Players;
		for (int I = NumPlayers; --I >= 0; ++PlayerState) {
			if (PlayerState->Velocity[0] || PlayerState->Velocity[1]) {
				int J = fabs(PlayerState->Velocity[0]) < fabs(PlayerState->Velocity[1]);
				double T = (PlayerState->Target[J] - PlayerState->Position[J]) / PlayerState->Velocity[J];
				if (Delta > T) {
					Delta = T;
					Update = UPDATE_PLAYER_STOP_MOVING;
					UpdatePlayer = PlayerState - State->Players;
				}
			}
			if (PlayerState->Rotation) {
				double T = (PlayerState->TargetAngle - PlayerState->Angle) / PlayerState->Rotation;
				if (Delta > T) {
					Delta = T;
					Update = UPDATE_PLAYER_STOP_TURNING;
					UpdatePlayer = PlayerState - State->Players;
				}
			}
		}

		// Check for player event
		if (Event) {
			double T = Event->Time - Time;
			if (Delta > T) {
				Delta = T;
				Update = UPDATE_PLAYER_EVENT;
			}
		}

		if (!isnan(State->Ball.Spawn)) {
			// The ball is not in play yet
			double T = State->Ball.Spawn - Time;
			if (Delta > T) {
				Delta = T;
				Update = UPDATE_BALL_SPAWN;
			}
		} else if (State->Ball.Handler >= 0) {
			// A player has the ball
			// Check for other players colliding with handler
			int Handler = State->Ball.Handler;
			player_state_t *HandlerState = State->Players + Handler;
			int Count = Game->TeamSize[0];
			if (Handler < Count) {
				PlayerState = State->Players + Count;
				Count = Game->TeamSize[1];
			} else {
				PlayerState = State->Players;
			}
			for (; --Count >= 0; ++PlayerState) {
				if (PlayerState->Velocity[0] || PlayerState->Velocity[1]) {
					//double2 PlayerInitial = PlayerState->Position;
					//double2 PlayerFinal = PlayerInitial + Delta * PlayerState->Velocity;
					// TODO: Rough check for possible collision

					double2 DX = HandlerState->Position - PlayerState->Position;
					double2 DV = HandlerState->Velocity - PlayerState->Velocity;
					double T = solve_quadratic(
						dot(DX, DX) - (PLAYER_RADIUS + PLAYER_RADIUS) * (PLAYER_RADIUS + PLAYER_RADIUS),
						2 * dot(DX, DV),
						dot(DV, DV)
					);
					if (Delta > T) {
						Delta = T;
						Update = UPDATE_PLAYER_TACKLE;
						UpdatePlayer = PlayerState - State->Players;
					}
				} else {
					// TODO: Rough check for possible collision

					double2 DX = HandlerState->Position - PlayerState->Position;
					double2 DV = HandlerState->Velocity;
					double T = solve_quadratic(
						dot(DX, DX) - (PLAYER_RADIUS + PLAYER_RADIUS) * (PLAYER_RADIUS + PLAYER_RADIUS),
						2 * dot(DX, DV),
						dot(DV, DV)
					);
					//printf("Collision with ball at %g\n", T);
					if (Delta > T) {
						Delta = T;
						Update = UPDATE_PLAYER_TACKLE;
						UpdatePlayer = PlayerState - State->Players;
					}
				}
			}
		} else if (State->Ball.Velocity[0] || State->Ball.Velocity[1]) {
			// Check for ball stopping due to friction
			int J = fabs(State->Ball.Friction[0]) < fabs(State->Ball.Friction[1]);
			double T = State->Ball.Velocity[J] / State->Ball.Friction[J];
			if (Delta > T) {
				Delta = T;
				Update = UPDATE_BALL_STOP;
			}
			// Compute ball bounding boxes
			double2 Initial = State->Ball.Position;
			double2 Final = Initial + Delta * State->Ball.Velocity - 0.5 * Delta * Delta * State->Ball.Friction;

			// Check for ball bouncing off walls
			if (Final[0] - BALL_RADIUS < -PITCH_WIDTH / 2) {
				// Left wall
				double A = -0.5 * State->Ball.Friction[0];
				double B = State->Ball.Velocity[0];
				double C = State->Ball.Position[0] + PITCH_WIDTH / 2 - BALL_RADIUS;
				double D = sqrt(B * B - 4 * A * C);
				double T = (-B - D) / (2 * A);
				if (Delta > T) {
					Delta = T;
					if (fabs(Final[1]) < GOAL_SIZE / 2) {
						Update = UPDATE_BALL_GOAL;
					} else {
						Update = UPDATE_BALL_WALL_X;
					}
					Final = Initial + Delta * State->Ball.Velocity - 0.5 * Delta * Delta * State->Ball.Friction;
				}
			} else if (Final[0] + BALL_RADIUS > PITCH_WIDTH / 2) {
				// Right wall
				double A = -0.5 * State->Ball.Friction[0];
				double B = State->Ball.Velocity[0];
				double C = State->Ball.Position[0] + BALL_RADIUS - PITCH_WIDTH / 2;
				double D = sqrt(B * B - 4 * A * C);
				double T = (-B + D) / (2 * A);
				if (Delta > T) {
					Delta = T;
					if (fabs(Final[1]) < GOAL_SIZE / 2) {
						Update = UPDATE_BALL_GOAL;
					} else {
						Update = UPDATE_BALL_WALL_X;
					}
					Final = Initial + Delta * State->Ball.Velocity - 0.5 * Delta * Delta * State->Ball.Friction;
				}
			}
			if (Final[1] - BALL_RADIUS < -PITCH_HEIGHT / 2) {
				// Top wall
				double A = -0.5 * State->Ball.Friction[1];
				double B = State->Ball.Velocity[1];
				double C = State->Ball.Position[1] + PITCH_HEIGHT / 2 - BALL_RADIUS;
				double D = sqrt(B * B - 4 * A * C);
				double T = (-B - D) / (2 * A);
				if (Delta > T) {
					Delta = T;
					Update = UPDATE_BALL_WALL_Y;
					Final = Initial + Delta * State->Ball.Velocity - 0.5 * Delta * Delta * State->Ball.Friction;
				}
			} else if (Final[1] + BALL_RADIUS > PITCH_HEIGHT / 2) {
				// Bottom wall
				double A = -0.5 * State->Ball.Friction[1];
				double B = State->Ball.Velocity[1];
				double C = State->Ball.Position[1] + BALL_RADIUS - PITCH_HEIGHT / 2;
				double D = sqrt(B * B - 4 * A * C);
				double T = (-B + D) / (2 * A);
				if (Delta > T) {
					Delta = T;
					Update = UPDATE_BALL_WALL_Y;
					Final = Initial + Delta * State->Ball.Velocity - 0.5 * Delta * Delta * State->Ball.Friction;
				}
			}

			// Check for ball colliding with player
			PlayerState = State->Players;
			for (int I = NumPlayers; --I >= 0; ++PlayerState) {
				if (PlayerState->Velocity[0] || PlayerState->Velocity[1]) {
					//double2 PlayerInitial = PlayerState->Position;
					//double2 PlayerFinal = PlayerInitial + Delta * PlayerState->Velocity;
					// TODO: Rough check for possible collision
					double2 DX = State->Ball.Position - PlayerState->Position;
					double2 DV = State->Ball.Velocity - PlayerState->Velocity;
					double2 F = State->Ball.Friction;
					double T = solve_quartic(
						dot(DX, DX) - (BALL_RADIUS + PLAYER_RADIUS) * (BALL_RADIUS + PLAYER_RADIUS),
						2 * dot(DX, DV),
						dot(DV, DV) - dot(DX, F),
						-dot(DV, F),
						dot(F, F) / 4
					);
					if (Delta > T) {
						Delta = T;
						Update = UPDATE_PLAYER_BALL;
						UpdatePlayer = PlayerState - State->Players;
					}
				} else {
					// TODO: Rough check for possible collision
					double2 DX = State->Ball.Position - PlayerState->Position;
					double2 DV = State->Ball.Velocity;
					double2 F = State->Ball.Friction;
					double T = solve_quartic(
						dot(DX, DX) - (BALL_RADIUS + PLAYER_RADIUS) * (BALL_RADIUS + PLAYER_RADIUS),
						2 * dot(DX, DV),
						dot(DV, DV) - dot(DX, F),
						-dot(DV, F),
						dot(F, F) / 4
					);
					//printf("Collision with ball at %g\n", T);
					if (Delta > T) {
						Delta = T;
						Update = UPDATE_PLAYER_BALL;
						UpdatePlayer = PlayerState - State->Players;
					}
				}
			}
		} else { // Ball is currently stationary
			// Check for players colliding with ball
			PlayerState = State->Players;
			for (int I = NumPlayers; --I >= 0; ++PlayerState) {
				if (PlayerState->Velocity[0] || PlayerState->Velocity[1]) {
					//double2 PlayerInitial = PlayerState->Position;
					//double2 PlayerFinal = PlayerInitial + Delta * PlayerState->Velocity;
					// TODO: Rough check for possible collision

					double2 DX = State->Ball.Position - PlayerState->Position;
					double2 DV = -PlayerState->Velocity;
					double T = solve_quadratic(
						dot(DX, DX) - (BALL_RADIUS + PLAYER_RADIUS) * (BALL_RADIUS + PLAYER_RADIUS),
						2 * dot(DX, DV),
						dot(DV, DV)
					);
					//printf("Collision with ball at %g\n", T);
					if (Delta > T) {
						Delta = T;
						Update = UPDATE_PLAYER_BALL;
						UpdatePlayer = PlayerState - State->Players;
					}
				}
			}
		}

		// Advance state before update
		if (State->Ball.Velocity[0] || State->Ball.Velocity[1]) {
			State->Ball.Position += Delta * State->Ball.Velocity - 0.5 * Delta * Delta * State->Ball.Friction;
			State->Ball.Velocity -= Delta * State->Ball.Friction;
		}
		for (player_t *Player = Game->Players; Player; Player = Player->Next) {
			player_state_t *PlayerState = State->Players + Player->Index;
			PlayerState->Position += Delta * PlayerState->Velocity;
			PlayerState->Angle += Delta * PlayerState->Rotation;
		}
		Time += Delta;

		//printf("Update @ %g[%g] -> %s\n", Time, Delta, UpdateNames[Update]);
		/*fprintf(Game->Log, "%g,%g,%s,%g,%g,%g,%g,%g,%g\n",
			Time, Delta, UpdateNames[Update],
			State->Ball.Position[0], State->Ball.Position[1],
			State->Ball.Velocity[0], State->Ball.Velocity[1],
			State->Ball.Friction[0], State->Ball.Friction[1]
		);*/
		switch (Update) {
		case UPDATE_NONE: {
			State->Time = Time;
			return;
		}
		case UPDATE_BALL_STOP: {
			State->Ball.Velocity = (double2){0, 0};
			State->Ball.Friction = (double2){0, 0};
			break;
		}
		case UPDATE_BALL_WALL_X: {
			State->Ball.Velocity[0] = -State->Ball.Velocity[0];
			State->Ball.Friction[0] = -State->Ball.Friction[0];
			break;
		}
		case UPDATE_BALL_WALL_Y: {
			State->Ball.Velocity[1] = -State->Ball.Velocity[1];
			State->Ball.Friction[1] = -State->Ball.Friction[1];
			break;
		}
		case UPDATE_BALL_GOAL: {
			int Team = State->Ball.Velocity[0] < 0;
			State->Score[Team] += 1;
			State->Ball.Spawn = Time + 1;
			printf("Goal! %d @ %f\n", Team, Time);
			break;
		}
		case UPDATE_BALL_SPAWN: {
			State->Ball.Spawn = NAN;
			State->Ball.Position = (double2){0, 0};
			State->Ball.Velocity = (double2){0, 0};
			State->Ball.Friction = (double2){0, 0};
			break;
		}
		case UPDATE_PLAYER_BALL: {
			State->Ball.Velocity = (double2){0, 0};
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
			State->Ball.Handler = UpdatePlayer;
			player_state_t *PlayerState = State->Players + UpdatePlayer;
#pragma GCC diagnostic pop
			PlayerState->Velocity *= (PLAYER_DRIBBLE_SPEED / PLAYER_RUN_SPEED);
			break;
		}
		case UPDATE_PLAYER_STOP_MOVING: {
			player_state_t *PlayerState = State->Players + UpdatePlayer;
			PlayerState->Velocity = (double2){0, 0};
			break;
		}
		case UPDATE_PLAYER_STOP_TURNING: {
			break;
		}
		case UPDATE_PLAYER_WALL_X: {
			break;
		}
		case UPDATE_PLAYER_WALL_Y: {
			break;
		}
		case UPDATE_PLAYER_TACKLE: {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
			player_state_t *PlayerState = State->Players + UpdatePlayer;
#pragma GCC diagnostic pop
			player_state_t *HandlerState = State->Players + State->Ball.Handler;
			double2 Segment = HandlerState->Position - PlayerState->Position;
			State->Ball.Handler = -1;
			State->Ball.Position = HandlerState->Position + Segment * ((PLAYER_RADIUS + PLAYER_RADIUS + BALL_RADIUS) / (PLAYER_RADIUS + PLAYER_RADIUS));
			State->Ball.Velocity = Segment * (BALL_KICK_SPEED / (PLAYER_RADIUS + PLAYER_RADIUS));
			State->Ball.Friction = State->Ball.Velocity * (BALL_FRICTION / BALL_KICK_SPEED);
			HandlerState->Velocity *= (PLAYER_RUN_SPEED / PLAYER_DRIBBLE_SPEED);
			break;
		}
		case UPDATE_PLAYER_EVENT: {
			player_state_t *PlayerState = State->Players + Event->Player;
			switch (Event->Action) {
			case ACTION_NONE: break;
			case ACTION_MOVE: {
				double2 Direction = Event->Target - PlayerState->Position;
				double Distance = sqrt(dot(Direction, Direction));
				if (Distance > 1e-10) {
					PlayerState->Target = Event->Target;
					if (State->Ball.Handler == Event->Player) {
						PlayerState->Velocity = Direction * (PLAYER_DRIBBLE_SPEED / Distance);
					} else {
						PlayerState->Velocity = Direction * (PLAYER_RUN_SPEED / Distance);
					}
				}
				break;
			}
			case ACTION_KICK: {
				if (State->Ball.Handler == Event->Player) {
					State->Ball.Handler = -1;
					State->Ball.Position = PlayerState->Position + PlayerState->Velocity * ((PLAYER_RADIUS + BALL_RADIUS) / PLAYER_DRIBBLE_SPEED);
					State->Ball.Velocity = PlayerState->Velocity * (BALL_KICK_SPEED / PLAYER_DRIBBLE_SPEED);
					State->Ball.Friction = State->Ball.Velocity * (BALL_FRICTION / BALL_KICK_SPEED);
					PlayerState->Velocity *= (PLAYER_RUN_SPEED / PLAYER_DRIBBLE_SPEED);
				}
				break;
			}
			}
			Event = Event->Next;
			break;
		}
		}
	}
}

void game_rebase(game_t *Game, double Target) {
	// Updates game base state to specified time (using game_predict).
	game_predict(Game, Target);
	game_state_t *State = Game->State;
	Game->State = Game->Base;
	Game->Base = State;
	event_t *Event = Game->Events;
	if (Event && Event->Time < Target) {
		event_t **Slot = &Event->Next;
		while (Slot[0] && Slot[0]->Time < Target) Slot = &Slot[0]->Next;
		Game->Events = Slot[0];
		Slot[0] = EventCache;
		EventCache = Event;
	}
	//fflush(Game->Log);
}

ML_TYPE(GameT, (), "game");
ML_TYPE(PlayerT, (), "player");
ML_ENUM2(ActionT, "action",
	"None", ACTION_NONE,
	"Move", ACTION_MOVE,
	"Kick", ACTION_KICK
);

ML_METHOD(GameT) {
	game_t *Game = game();
	Game->Type = GameT;
	return (ml_value_t *)Game;
}

ML_METHOD("size", GameT) {
	return ml_tuplev(2, ml_real(PITCH_WIDTH), ml_real(PITCH_HEIGHT));
}

ML_METHOD("start", GameT) {
	game_t *Game = (game_t *)Args[0];
	game_start(Game);
	return (ml_value_t *)Game;
}

ML_METHOD("player", GameT, MLIntegerT) {
	game_t *Game = (game_t *)Args[0];
	int Team = ml_integer_value(Args[1]);
	if (Team != 1 && Team != 2) return ml_error("ValueError", "Invalid team number");
	player_t *Player = game_player(Game, Team - 1);
	Player->Type = PlayerT;
	return (ml_value_t *)Player;
}

ML_METHOD("team", PlayerT) {
	player_t *Player = (player_t *)Args[0];
	return ml_integer(Player->Team + 1);
}

ML_METHOD("index", PlayerT) {
	player_t *Player = (player_t *)Args[0];
	return ml_integer(Player->Index);
}

ML_METHOD("event", PlayerT, MLRealT, MLRealT, MLRealT, ActionT) {
	player_t *Player = (player_t *)Args[0];
	player_event(Player, ml_real_value(Args[1]), (double2){ml_real_value(Args[2]), ml_real_value(Args[3])}, ml_enum_value_value(Args[4]));
	return (ml_value_t *)Player;
}

ML_METHOD("predict", GameT, MLRealT) {
	game_t *Game = (game_t *)Args[0];
	if (!Game->State) return ml_error("StateError", "Game not started yet");
	game_predict(Game, ml_real_value(Args[1]));
	game_state_t *State = Game->State;
	ml_value_t *Result = ml_list();
	player_state_t *PlayerState = State->Players;
	for (int I = Game->NumPlayers; --I >= 0; ++PlayerState) {
		ml_value_t *PlayerResult = ml_list();
		ml_list_put(PlayerResult, ml_real(PlayerState->Position[0]));
		ml_list_put(PlayerResult, ml_real(PlayerState->Position[1]));
		ml_list_put(PlayerResult, ml_real(PlayerState->Velocity[0]));
		ml_list_put(PlayerResult, ml_real(PlayerState->Velocity[1]));
		ml_list_put(PlayerResult, ml_real(PlayerState->Target[0]));
		ml_list_put(PlayerResult, ml_real(PlayerState->Target[1]));
		ml_list_put(Result, PlayerResult);
	}
	if (!isnan(State->Ball.Spawn)) {
		ml_list_push(Result, MLNil);
	} else if (State->Ball.Handler >= 0) {
		ml_list_push(Result, ml_integer(State->Ball.Handler + 1));
	} else {
		ml_value_t *BallResult = ml_list();
		ml_list_put(BallResult, ml_real(State->Ball.Position[0]));
		ml_list_put(BallResult, ml_real(State->Ball.Position[1]));
		ml_list_put(BallResult, ml_real(State->Ball.Velocity[0]));
		ml_list_put(BallResult, ml_real(State->Ball.Velocity[1]));
		ml_list_put(BallResult, ml_real(State->Ball.Friction[0]));
		ml_list_put(BallResult, ml_real(State->Ball.Friction[1]));
		ml_list_push(Result, BallResult);
	}
	ml_value_t *ScoreResult = ml_list();
	ml_list_put(ScoreResult, ml_integer(State->Score[0]));
	ml_list_put(ScoreResult, ml_integer(State->Score[1]));
	ml_list_push(Result, ScoreResult);
	ml_list_push(Result, ml_real(State->Time));
	return Result;
}

ML_METHOD("rebase", GameT, MLRealT) {
	game_t *Game = (game_t *)Args[0];
	game_rebase(Game, ml_real_value(Args[1]));
	return Args[1];
}

void ml_library_entry0(ml_value_t **Slot) {
#include "engine_init.c"
	stringmap_insert(GameT->Exports, "player", PlayerT);
	stringmap_insert(GameT->Exports, "action", ActionT);
	Slot[0] = (ml_value_t *)GameT;
}
