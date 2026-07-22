# Oracle Bridge

CommunicationMod-driven harness (InitialPlan.md B.1): drives the desktop game
headlessly-ish to start runs on a chosen seed, inject action sequences, and
dump full game state as JSON after every action, which a translator maps into
the binary schema shared with the simulator. Built before mass rule
implementation begins, since every seed-replay differential test rides on it.

Layout (all Windows-host, excluded from WSL CI):

- [PROTOCOL.md](PROTOCOL.md) — the CommunicationMod wire protocol + full
  field-disposition catalogue, surveyed from source at B0.1.
- [communicationmod-oracle/](communicationmod-oracle/) — the vendored fork
  (upstream v1.2.1, MIT). Fork docs: `README-oracle.md`.
- [build_fork.ps1](build_fork.ps1) — JDK-8 fork build pipeline (B1.1);
  deterministic jar, deployed to the game's `mods\` directory.
- [driver/](driver/) — the Python child process CommunicationMod spawns
  (`echo_driver.py` since B0.2; grows into the campaign driver at B1.4).
