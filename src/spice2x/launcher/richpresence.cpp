#include "richpresence.h"
#include <external/robin_hood.h>
#include "external/discord-rpc/include/discord_rpc.h"
#include "util/logging.h"
#include "misc/eamuse.h"

namespace richpresence {

    namespace discord {

        // application IDs
        static robin_hood::unordered_map<std::string, std::string> APP_IDS = {
                {"Sound Voltex", "1225989533317992509"},
                {"Beatmania IIDX", "1225993043010912258"},
                {"Jubeat", "1226662675497484288"},
                {"Dance Evolution", "1226662773010727003"},
                {"Beatstream", "1226664029666152600"},
                {"Metal Gear", "1226664830178693291"},
                {"Reflec Beat", "1226666988450087012"},
                {"Pop'n Music", "1226667130033016922"},
                {"Steel Chronicle", "1226669022859231293"},
                {"Road Fighters 3D", "1226669786017042493"},
                {"Museca", "1226669886579802252"},
                {"Bishi Bashi Channel", "1226671221467512853"},
                {"GitaDora", "1226671586661371945"},
                {"Dance Dance Revolution", "1226672373143699456"},
                {"Nostalgia", "1226680552963309618"},
                {"Quiz Magic Academy", "1226681569989754941"},
                {"FutureTomTom", "1226693733484068974"},
                {"Mahjong Fight Club", "1226693952829128714"},
                {"HELLO! Pop'n Music", "1226695294838898761"},
                {"LovePlus", "1226702489659641896"},
                {"Tenkaichi Shogikai", "1226703627687559218"},
                {"DANCERUSH", "1226709135828193282"},
                {"Scotto", "1226716024305619016"},
                {"Winning Eleven", "1226721709500137574"},
                {"Otoca D'or", "1226737298285133836"},
                {"Charge Machine", "1226739364126654516"},
                {"Ongaku Paradise", "1226739545559531621"},
                {"Busou Shinki: Armored Princess Battle Conductor", "1226739666741366916"},
                {"Chase Chase Jokers", "1226739863915593770"},
                {"QuizKnock STADIUM", "1226739930328334478"},
                {"Mahjong Fight Girl", "1417371188342030468"},
                {"Polaris Chord", "1417371592224407552"},
        };

        // state
        std::string APPID_OVERRIDE = "";
        bool INITIALIZED = false;

        void ready(const DiscordUser *request) {
            log_warning("richpresence:discord", "ready");
        }

        void disconnected(int errorCode, const char *message) {
            log_warning("richpresence:discord", "disconnected");
        }

        void errored(int errorCode, const char *message) {
            log_warning("richpresence:discord", "error {}: {}", errorCode, message);
        }

        void joinGame(const char *joinSecret) {
            log_warning("richpresence:discord", "joinGame");
        }

        void spectateGame(const char *spectateSecret) {
            log_warning("richpresence:discord", "spectateGame");
        }

        void joinRequest(const DiscordUser *request) {
            log_warning("richpresence:discord", "joinRequest");
        }

        // handler object
        static DiscordEventHandlers handlers {
                .ready = discord::ready,
                .disconnected = discord::disconnected,
                .errored = discord::errored,
                .joinGame = discord::joinGame,
                .spectateGame = discord::spectateGame,
                .joinRequest = discord::joinRequest
        };

        void update() {

            // check state
            if (!INITIALIZED)
                return;

            // update presence
            DiscordRichPresence presence {};
            presence.startTimestamp = std::time(nullptr);
            Discord_UpdatePresence(&presence);
        }

        void init() {

            // check state
            if (INITIALIZED) {
                return;
            }

            // get id
            std::string id = "";
            if (!APPID_OVERRIDE.empty()) {
                log_info("richpresence:discord", "using custom APPID: {}", APPID_OVERRIDE);
                id = APPID_OVERRIDE;
            } else {
                auto game_model = eamuse_get_game();
                if (game_model.empty()) {
                    log_warning("richpresence:discord", "could not get game model");
                    return;
                }

                id = APP_IDS[game_model];
                if (id.empty()) {
                    log_warning("richpresence:discord", "did not find app ID for {}", game_model);
                    return;
                }
            }

            // initialize discord
            Discord_Initialize(id.c_str(), &discord::handlers, 0, nullptr);

            // mark as initialized
            INITIALIZED = true;
            log_info("richpresence:discord", "initialized");

            // update once so the presence is displayed
            update();
        }

        void shutdown() {
            Discord_ClearPresence();
            Discord_Shutdown();
        }
    }

    // state
    static bool INITIALIZED = false;

    void init() {
        if (INITIALIZED)
            return;
        log_info("richpresence", "initializing");
        INITIALIZED = true;
        discord::init();
    }

    void update(const char *state) {
        if (!INITIALIZED)
            return;
        discord::update();
    }

    void shutdown() {
        if (!INITIALIZED)
            return;
        log_info("richpresence", "shutdown");
        discord::shutdown();
        INITIALIZED = false;
    }
}
