# Oracle Bridge

CommunicationMod-driven harness (InitialPlan.md B.1): drives the desktop game
headlessly-ish to start runs on a chosen seed, inject action sequences, and
dump full game state as JSON after every action, which a translator maps into
the binary schema shared with the simulator. Built before mass rule
implementation begins, since every seed-replay differential test rides on it.
