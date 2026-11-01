#include <dpp/dpp.h>
#include <cstdlib>
#include <iostream>

int main() {
    const char* token = std::getenv("DISCORD_TOKEN");
    if (token == nullptr || *token == '\0') {
        std::cerr << "DISCORD_TOKEN is not set. Put it in .env and load it first.\n";
        return 1;
    }

    dpp::cluster bot(token);
    bot.on_log(dpp::utility::cout_logger());

    bot.on_slashcommand([](const dpp::slashcommand_t& event) {
        if (event.command.get_command_name() == "ping") {
            event.reply("Pong!");
        }
    });

    bot.on_ready([&bot](const dpp::ready_t& event) {
        if (dpp::run_once<struct register_bot_commands>()) {
            bot.global_command_create(dpp::slashcommand("ping", "Ping pong!", bot.me.id));
        }
    });

    bot.start(dpp::st_wait);
    return 0;
}
