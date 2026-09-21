# User-facing commands

## Board2 proof and vector tests

```sh
make test-board2
```

Checks the independent proof blobs when needed, then runs the assertion-enabled
Board2 vector tests.

## Board2 million-game stress test

```sh
make test-board2-stress
```

Runs one million deterministic games through the assertion-enabled Board2
implementation and its independent reference model.
