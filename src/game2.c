#include <minilang/ml_library.h>
#include <minilang/ml_macros.h>
#include <minilang/ml_object.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <complex.h>
#undef I

typedef struct event_t event_t;
typedef struct player_t player_t;
typedef struct game_t game_t;

typedef enum {ACTION_NONE, ACTION_SHORT_KICK, ACTION_MEDIUM_KICK, ACTION_LONG_KICK} action_t;

typedef double double2 __attribute__((vector_size(16)));

static inline double dot(double2 A, double2 B) {
	return A[0] * B[0] + A[1] * B[1];
}

struct event_t {
	event_t *Next;
	player_t *Player;
	double2 Target;
	double Time;
	action_t Action;
};

typedef struct {
	double2 Position, Velocity, Target;
	double Angle, Rotation, TargetAngle;
} player_state_t;

typedef struct {
	struct {
		player_t *Handler;
		double2 Position, Velocity, Friction;
	} Ball;
	double Time;
	player_state_t Players[];
} game_state_t;

struct game_t {
	ml_type_t *Type;
	FILE *Log;
	player_t *Players;
	event_t *Events;
	game_state_t *Base, *State;
	int NumPlayers, StateSize;
};

struct player_t {
	ml_type_t *Type;
	player_t *Next;
	game_t *Game;
	int Index, Team;
};

#define PITCH_WIDTH 180
#define PITCH_HEIGHT 100

#define BALL_RADIUS 1
#define PLAYER_RADIUS 5

#define BALL_FRICTION 5

#define PLAYER_RUN_SPEED 20
#define PLAYER_BALL_FORCE 10

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
	int NumPlayers = 0;
	for (player_t *Player = Game->Players; Player; Player = Player->Next) Player->Index = NumPlayers++;
	Game->NumPlayers = NumPlayers;
	int StateSize = Game->StateSize = sizeof(game_state_t) + NumPlayers * sizeof(player_state_t);
	game_state_t *State = Game->Base = (game_state_t *)snew(StateSize);
	player_state_t *PlayerState = State->Players;
	for (player_t *Player = Game->Players; Player; Player = Player->Next, ++PlayerState) {
		double2 Position = {
			0.9 * PITCH_WIDTH * ((double)rand() / RAND_MAX - 0.5),
			0.9 * PITCH_HEIGHT * ((double)rand() / RAND_MAX - 0.5)
		};
		PlayerState->Position = PlayerState->Target = Position;
		PlayerState->Velocity = (double2){0, 0};
	}
	State->Ball.Position = (double2){0, 0};
	State->Ball.Velocity = (double2){0, 0};
	State->Ball.Friction = (double2){0, 0};
	//State->Ball.Velocity = (double2){17, 15};
	//double2 Direction = State->Ball.Velocity / sqrt(dot(State->Ball.Velocity, State->Ball.Velocity));
	//State->Ball.Friction = Direction * BALL_FRICTION;
	State->Ball.Handler = NULL;
	State->Time = 0;
	Game->State = (game_state_t *)snew(StateSize);
	Game->Log = fopen("log.csv", "w");
	fprintf(Game->Log, "time,delta,update,px,py,vx,vy,fx,fy\n");
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
	Event->Player = Player;
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
	complex double Z[4] = {-1, 1, -1i, 1i};
	for (int I = 0; I < 20; ++I) {
		complex double PZ[4], QZ[4], RZ[4];
		for (int I = 0; I < 4; ++I) {
			PZ[I] = P0 + Z[I] * (P1 + Z[I] * (P2 + Z[I] * (P3 + Z[I] * P4)));
			QZ[I] = P1 + Z[I] * (2 * P2 + Z[I] * (3 * P3 + Z[I] * 4 * P4));
			RZ[I] = 1 / (Z[I] - Z[(I + 1) % 4]) + 1 / (Z[I] - Z[(I + 2) % 4]) + 1 / (Z[I] - Z[(I + 3) % 4]);
		}
		for (int I = 0; I < 4; ++I) Z[I] -= PZ[I] / (QZ[I] - PZ[I] * RZ[I]);
	}
	double T = INFINITY;
	for (int I = 0; I < 4; ++I) {
		//printf("\tt = %g + %gi\n", creal(Z[I]), cimag(Z[I]));
		if (fabs(cimag(Z[I])) < 1e-9) {
			double T1 = creal(Z[I]);
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
	UPDATE_BALL_PLAYER,
	UPDATE_PLAYER_STOP_MOVING,
	UPDATE_PLAYER_STOP_TURNING,
	UPDATE_PLAYER_WALL_X,
	UPDATE_PLAYER_WALL_Y,
	UPDATE_PLAYER_PLAYER,
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
	for (;;) {
		double Delta = Target - Time;
		update_t Update = UPDATE_NONE;
		player_state_t *UpdatePlayer, *UpdatePlayer2;

		// Check for next update
		// 1. Check for ball events (collision / stopping due to friction)
		// 2. Check for player events (collision / reaching target)
		// 3. Check for next player event

		// Check for ball stopping due to friction
		if (State->Ball.Velocity[0] || State->Ball.Velocity[1]) {
			int I = fabs(State->Ball.Friction[0]) < fabs(State->Ball.Friction[1]);
			double T = State->Ball.Velocity[I] / State->Ball.Friction[I];
			if (Delta > T) {
				Delta = T;
				Update = UPDATE_BALL_STOP;
			}
		}

		// Check for players stopping due to reaching their target
		player_state_t *PlayerState = State->Players;
		for (int I = Game->NumPlayers; --I >= 0; ++PlayerState) {
			if (PlayerState->Velocity[0] || PlayerState->Velocity[1]) {
				int I = fabs(PlayerState->Velocity[0]) < fabs(PlayerState->Velocity[1]);
				double T = (PlayerState->Target[I] - PlayerState->Position[I]) / PlayerState->Velocity[I];
				if (Delta > T) {
					Delta = T;
					Update = UPDATE_PLAYER_STOP_MOVING;
					UpdatePlayer = PlayerState;
				}
			}
			if (PlayerState->Rotation) {
				double T = (PlayerState->TargetAngle - PlayerState->Angle) / PlayerState->Rotation;
				if (Delta > T) {
					Delta = T;
					Update = UPDATE_PLAYER_STOP_TURNING;
					UpdatePlayer = PlayerState;
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

		if (State->Ball.Velocity[0] || State->Ball.Velocity[1]) {
			// Compute ball bounding boxes
			double2 Initial = State->Ball.Position;
			double2 Final = Initial + Delta * State->Ball.Velocity - 0.5 * Delta * Delta * State->Ball.Friction;

			// Check for ball bouncing off walls
			if (Final[0] - BALL_RADIUS < -PITCH_WIDTH / 2) {
				double A = -0.5 * State->Ball.Friction[0];
				double B = State->Ball.Velocity[0];
				double C = State->Ball.Position[0] + PITCH_WIDTH / 2 - BALL_RADIUS;
				double D = sqrt(B * B - 4 * A * C);
				double T = (-B - D) / (2 * A);
				if (Delta > T) {
					Delta = T;
					Update = UPDATE_BALL_WALL_X;
					Final = Initial + Delta * State->Ball.Velocity - 0.5 * Delta * Delta * State->Ball.Friction;
				}
			} else if (Final[0] + BALL_RADIUS > PITCH_WIDTH / 2) {
				double A = -0.5 * State->Ball.Friction[0];
				double B = State->Ball.Velocity[0];
				double C = State->Ball.Position[0] + BALL_RADIUS - PITCH_WIDTH / 2;
				double D = sqrt(B * B - 4 * A * C);
				double T = (-B + D) / (2 * A);
				if (Delta > T) {
					Delta = T;
					Update = UPDATE_BALL_WALL_X;
					Final = Initial + Delta * State->Ball.Velocity - 0.5 * Delta * Delta * State->Ball.Friction;
				}
			}
			if (Final[1] - BALL_RADIUS < -PITCH_HEIGHT / 2) {
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
			for (int I = Game->NumPlayers; --I >= 0; ++PlayerState) {
				if (PlayerState->Velocity[0] || PlayerState->Velocity[1]) {
					double2 PlayerInitial = PlayerState->Position;
					double2 PlayerFinal = PlayerInitial + Delta * PlayerState->Velocity;
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
						Update = UPDATE_BALL_PLAYER;
						UpdatePlayer = PlayerState;
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
					printf("Collision with ball at %g\n", T);
					if (Delta > T) {
						Delta = T;
						Update = UPDATE_BALL_PLAYER;
						UpdatePlayer = PlayerState;
					}
				}
			}
		} else { // Ball is currently stationary
			// Check for ball colliding with player
			PlayerState = State->Players;
			for (int I = Game->NumPlayers; --I >= 0; ++PlayerState) {
				if (PlayerState->Velocity[0] || PlayerState->Velocity[1]) {
					double2 PlayerInitial = PlayerState->Position;
					double2 PlayerFinal = PlayerInitial + Delta * PlayerState->Velocity;
					// TODO: Rough check for possible collision

					double2 DX = State->Ball.Position - PlayerState->Position;
					double2 DV = -PlayerState->Velocity;
					double T = solve_quadratic(
						dot(DX, DX) - (BALL_RADIUS + PLAYER_RADIUS) * (BALL_RADIUS + PLAYER_RADIUS),
						2 * dot(DX, DV),
						dot(DV, DV)
					);
					printf("Collision with ball at %g\n", T);
					if (Delta > T) {
						Delta = T;
						Update = UPDATE_BALL_PLAYER;
						UpdatePlayer = PlayerState;
					}
				}
			}
		}

		// Advance state before update
		if (State->Ball.Velocity[0] || State->Ball.Velocity[1]) {
			State->Ball.Position += Delta * State->Ball.Velocity - 0.5 * Delta * Delta * State->Ball.Friction;
			State->Ball.Velocity -= Delta * State->Ball.Friction;
		}
		PlayerState = State->Players;
		for (int I = Game->NumPlayers; --I >= 0; ++PlayerState) {
			PlayerState->Position += Delta * PlayerState->Velocity;
			PlayerState->Angle += Delta * PlayerState->Rotation;
		}
		Time += Delta;

		printf("Update @ %g[%g] -> %s\n", Time, Delta, UpdateNames[Update]);
		fprintf(Game->Log, "%g,%g,%s,%g,%g,%g,%g,%g,%g\n",
			Time, Delta, UpdateNames[Update],
			State->Ball.Position[0], State->Ball.Position[1],
			State->Ball.Velocity[0], State->Ball.Velocity[1],
			State->Ball.Friction[0], State->Ball.Friction[1]
		);
		switch (Update) {
		case UPDATE_NONE:
			State->Time = Time;
			return;
		case UPDATE_BALL_STOP:
			State->Ball.Velocity = (double2){0, 0};
			State->Ball.Friction = (double2){0, 0};
			break;
		case UPDATE_BALL_WALL_X:
			State->Ball.Velocity[0] = -State->Ball.Velocity[0];
			State->Ball.Friction[0] = -State->Ball.Friction[0];
			break;
		case UPDATE_BALL_WALL_Y:
			State->Ball.Velocity[1] = -State->Ball.Velocity[1];
			State->Ball.Friction[1] = -State->Ball.Friction[1];
			break;
		case UPDATE_BALL_GOAL:
			break;
		case UPDATE_BALL_PLAYER: {
			double2 Direction = State->Ball.Position - UpdatePlayer->Position;
			//State->Ball.Velocity += Direction * PLAYER_BALL_FORCE;
			State->Ball.Velocity = Direction * PLAYER_BALL_FORCE;
			double Speed = sqrt(dot(State->Ball.Velocity, State->Ball.Velocity));
			if (Speed > 1e-10) {
				State->Ball.Friction = State->Ball.Velocity * (BALL_FRICTION / Speed);
			}
			break;
		}
		case UPDATE_PLAYER_STOP_MOVING:
			UpdatePlayer->Velocity = (double2){0, 0};
			break;
		case UPDATE_PLAYER_STOP_TURNING:
			break;
		case UPDATE_PLAYER_WALL_X:
			break;
		case UPDATE_PLAYER_WALL_Y:
			break;
		case UPDATE_PLAYER_PLAYER:
			break;
		case UPDATE_PLAYER_EVENT: {
			UpdatePlayer = &State->Players[Event->Player->Index];
			double2 Direction = Event->Target - UpdatePlayer->Position;
			double Distance = sqrt(dot(Direction, Direction));
			if (Distance > 1e-10) {
				UpdatePlayer->Target = Event->Target;
				UpdatePlayer->Velocity = Direction * (PLAYER_RUN_SPEED / Distance);
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
	fflush(Game->Log);
}

ML_TYPE(GameT, (), "game");
ML_TYPE(PlayerT, (), "player");
ML_ENUM2(ActionT, "action",
	"None", ACTION_NONE,
	"ShortKick", ACTION_SHORT_KICK,
	"MediumKick", ACTION_MEDIUM_KICK,
	"LongKick", ACTION_LONG_KICK
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
	player_t *Player = game_player(Game, ml_integer_value(Args[1]));
	Player->Type = PlayerT;
	return (ml_value_t *)Player;
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
	ml_list_put(Result, ml_tuplev(7,
		ml_real(State->Ball.Position[0]), ml_real(State->Ball.Position[1]),
		ml_real(State->Ball.Velocity[0]), ml_real(State->Ball.Velocity[1]),
		ml_real(State->Ball.Friction[0]), ml_real(State->Ball.Friction[1]),
		State->Ball.Handler ? ml_integer(State->Ball.Handler->Index) : MLNil
	));
	player_state_t *PlayerState = State->Players;
	for (player_t *Player = Game->Players; Player; Player = Player->Next, ++PlayerState) {
		ml_list_put(Result, ml_tuplev(6,
			ml_real(PlayerState->Position[0]), ml_real(PlayerState->Position[1]),
			ml_real(PlayerState->Velocity[0]), ml_real(PlayerState->Velocity[1]),
			ml_real(PlayerState->Target[0]), ml_real(PlayerState->Target[1])
		));
	}
	return Result;
}

ML_METHOD("rebase", GameT, MLRealT) {
	game_t *Game = (game_t *)Args[0];
	game_rebase(Game, ml_real_value(Args[1]));
	return (ml_value_t *)Game;
}

void ml_library_entry0(ml_value_t **Slot) {
#include "game2_init.c"
	stringmap_insert(GameT->Exports, "player", PlayerT);
	stringmap_insert(GameT->Exports, "action", ActionT);
	Slot[0] = (ml_value_t *)GameT;
}
