# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

fin-tracker-bot is a personal Discord bot for tracking expenses, written in C++.

## Stack

- C++20, built with CMake.
- Discord library: [D++ (DPP)](https://dpp.dev) 10.x, installed system-wide from the official `.deb` and found with `find_package(dpp)`. It is not vendored or fetched by CMake. Its CMake config also needs `libopus-dev` installed, even though the bot doesn't use voice.
- No tests or linter yet.

## Commands

```bash
cmake -B build && cmake --build build   # build
set -a; source .env; set +a             # load DISCORD_TOKEN into the shell
./build/fin-tracker-bot                 # run
```

## Structure

- `src/main.cpp`: entry point. It reads `DISCORD_TOKEN` from the environment, creates a `dpp::cluster`, registers global slash commands once in `on_ready` (guarded by `dpp::run_once`), and dispatches them in `on_slashcommand`. New commands follow the same pattern.

## Configuration

The bot token lives in `.env` (git-ignored); `.env.example` shows the format. The program doesn't parse `.env` itself, so the shell must export it before running.
