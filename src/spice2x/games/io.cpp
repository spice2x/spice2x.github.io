#include "io.h"

#include <external/robin_hood.h>

#include "bbc/io.h"
#include "bs/io.h"
#include "ddr/io.h"
#include "dea/io.h"
#include "drs/io.h"
#include "ftt/io.h"
#include "gitadora/io.h"
#include "hpm/io.h"
#include "iidx/io.h"
#include "jb/io.h"
#include "launcher/launcher.h"
#include "launcher/options.h"
#include "loveplus/io.h"
#include "mfc/io.h"
#include "mga/io.h"
#include "museca/io.h"
#include "nost/io.h"
#include "otoca/io.h"
#include "popn/io.h"
#include "qma/io.h"
#include "rb/io.h"
#include "rf3d/io.h"
#include "sc/io.h"
#include "scotto/io.h"
#include "sdvx/io.h"
#include "shogikai/io.h"
#include "silentscope/io.h"
#include "we/io.h"
#include "pcm/io.h"
#include "onpara/io.h"
#include "bc/io.h"
#include "ccj/io.h"
#include "qks/io.h"
#include "mfg/io.h"
#include "pc/io.h"

namespace games {

    // state
    static bool IO_INITIALIZED = false;
    static std::vector<std::string> games;
    static robin_hood::unordered_map<std::string, std::vector<Button> &> buttons;
    static robin_hood::unordered_map<std::string, std::vector<Button>> buttons_keypads;
    static robin_hood::unordered_map<std::string, std::vector<Button>> buttons_overlay;
    static robin_hood::unordered_map<std::string, std::string> buttons_help;
    static robin_hood::unordered_map<std::string, std::string> analogs_help;
    static robin_hood::unordered_map<std::string, std::vector<Analog> &> analogs;
    static robin_hood::unordered_map<std::string, std::vector<Light> &> lights;
    static robin_hood::unordered_map<std::string, std::vector<Option>> options;
    static robin_hood::unordered_map<std::string, std::vector<std::string>> file_hints;

    static void initialize() {

        // check if already done
        if (IO_INITIALIZED) {
            return;
        }
        IO_INITIALIZED = true;

        // bbc
        const std::string bbc("Bishi Bashi Channel");
        games.push_back(bbc);
        buttons.insert({ bbc, bbc::get_buttons() });
        buttons_help.insert({ bbc, bbc::get_buttons_help() });
        analogs.insert({ bbc, bbc::get_analogs() });
        lights.insert({ bbc, bbc::get_lights() });
        file_hints[bbc].emplace_back("bsch.dll");

        // hpm
        const std::string hpm("HELLO! Pop'n Music");
        games.push_back(hpm);
        buttons.insert({ hpm, hpm::get_buttons() });
        buttons_help.insert({ hpm, hpm::get_buttons_help() });
        lights.insert({ hpm, hpm::get_lights() });
        file_hints[hpm].emplace_back("popn.dll");

        // bs
        const std::string bs("Beatstream");
        games.push_back(bs);
        buttons.insert({ bs, bs::get_buttons() });
        lights.insert({ bs, bs::get_lights() });
        file_hints[bs].emplace_back("beatstream.dll");
        file_hints[bs].emplace_back("beatstream1.dll");
        file_hints[bs].emplace_back("beatstream2.dll");

        // ddr
        const std::string ddr("Dance Dance Revolution");
        games.push_back(ddr);
        buttons.insert({ ddr, ddr::get_buttons() });
        lights.insert({ ddr, ddr::get_lights() });
        file_hints[ddr].emplace_back("ddr.dll");
        file_hints[ddr].emplace_back("mdxja_945.dll");
        file_hints[ddr].emplace_back("arkmdxp3.dll");

        // dea
        const std::string dea("Dance Evolution");
        games.push_back(dea);
        buttons.insert({ dea, dea::get_buttons() });
        lights.insert({ dea, dea::get_lights() });
        file_hints[dea].emplace_back("arkkdm.dll");

        // gitadora
        const std::string gitadora("GitaDora");
        games.push_back(gitadora);
        buttons.insert({ gitadora, gitadora::get_buttons() });
        buttons_help.insert({ gitadora, gitadora::get_buttons_help() });
        analogs_help.insert({ gitadora, gitadora::get_analogs_help() });
        analogs.insert({ gitadora, gitadora::get_analogs() });
        lights.insert({ gitadora, gitadora::get_lights() });
        file_hints[gitadora].emplace_back("gdxg.dll");

        // iidx
        const std::string iidx("Beatmania IIDX");
        games.push_back(iidx);
        buttons.insert({ iidx, iidx::get_buttons() });
        buttons_help.insert({ iidx, iidx::get_buttons_help() });
        analogs.insert({ iidx, iidx::get_analogs() });
        lights.insert({ iidx, iidx::get_lights() });
        file_hints[iidx].emplace_back("bm2dx.dll");

        // jb
        const std::string jb("Jubeat");
        games.push_back(jb);
        buttons.insert({ jb, jb::get_buttons() });
        buttons_help.insert({ jb, jb::get_buttons_help() });
        lights.insert({ jb, jb::get_lights() });
        file_hints[jb].emplace_back("jubeat.dll");

        // mga
        const std::string mga("Metal Gear");
        games.push_back(mga);
        buttons.insert({ mga, mga::get_buttons() });
        analogs.insert({ mga, mga::get_analogs() });
        lights.insert({ mga, mga::get_lights() });
        file_hints[mga].emplace_back("launch.dll");

        // museca
        const std::string museca("Museca");
        games.push_back(museca);
        buttons.insert({ museca, museca::get_buttons() });
        buttons_help.insert({ museca, museca::get_buttons_help() });
        analogs.insert({ museca, museca::get_analogs() });
        lights.insert({ museca, museca::get_lights() });
        file_hints[museca].emplace_back("museca.dll");

        // nost
        const std::string nost("Nostalgia");
        games.push_back(nost);
        buttons.insert({ nost, nost::get_buttons() });
        buttons_help.insert({ nost, nost::get_buttons_help() });
        analogs.insert({ nost, nost::get_analogs() });
        lights.insert({ nost, nost::get_lights() });
        file_hints[nost].emplace_back("nostalgia.dll");

        // popn
        const std::string popn("Pop'n Music");
        games.push_back(popn);
        buttons.insert({ popn, popn::get_buttons() });
        buttons_help.insert({ popn, popn::get_buttons_help() });
        lights.insert({ popn, popn::get_lights() });
        file_hints[popn].emplace_back("popn19.dll");
        file_hints[popn].emplace_back("popn20.dll");
        file_hints[popn].emplace_back("popn21.dll");
        file_hints[popn].emplace_back("popn22.dll");
        file_hints[popn].emplace_back("popn23.dll");
        file_hints[popn].emplace_back("popn24.dll");
        file_hints[popn].emplace_back("popn25.dll");

        // qma
        const std::string qma("Quiz Magic Academy");
        games.push_back(qma);
        buttons.insert({ qma, qma::get_buttons() });
        lights.insert({ qma, qma::get_lights() });
        file_hints[qma].emplace_back("client.dll");

        // rb
        const std::string rb("Reflec Beat");
        games.push_back(rb);
        buttons.insert({ rb, rb::get_buttons() });
        lights.insert({ rb, rb::get_lights() });
        file_hints[rb].emplace_back("reflecbeat.dll");

        // shogikai
        std::string shogikai("Tenkaichi Shogikai");
        games.push_back(shogikai);
        buttons.insert({ shogikai, shogikai::get_buttons() });
        lights.insert({ shogikai, shogikai::get_lights() });
        file_hints[shogikai].emplace_back("shogi_engine.dll");

        // rf3d
        const std::string rf3d("Road Fighters 3D");
        games.push_back(rf3d);
        buttons.insert({ rf3d, rf3d::get_buttons() });
        analogs.insert({ rf3d, rf3d::get_analogs() });
        file_hints[rf3d].emplace_back("jgt.dll");

        // sc
        const std::string sc("Steel Chronicle");
        games.push_back(sc);
        buttons.insert({ sc, sc::get_buttons() });
        analogs.insert({ sc, sc::get_analogs() });
        lights.insert({ sc, sc::get_lights() });
        file_hints[sc].emplace_back("gamekgg.dll");

        // sdvx
        const std::string sdvx("Sound Voltex");
        games.push_back(sdvx);
        buttons.insert({ sdvx, sdvx::get_buttons() });
        buttons_help.insert({ sdvx, sdvx::get_buttons_help() });
        analogs.insert({ sdvx, sdvx::get_analogs() });
        lights.insert({ sdvx, sdvx::get_lights() });
        file_hints[sdvx].emplace_back("soundvoltex.dll");

        // mfc
        const std::string mfc("Mahjong Fight Club");
        games.push_back(mfc);
        buttons.insert({ mfc, mfc::get_buttons() });
        file_hints[mfc].emplace_back("allinone.dll");

        // ftt
        const std::string ftt("FutureTomTom");
        games.push_back(ftt);
        buttons.insert({ ftt, ftt::get_buttons() });
        buttons_help.insert({ ftt, ftt::get_buttons_help() });
        analogs.insert({ ftt, ftt::get_analogs() });
        lights.insert({ ftt, ftt::get_lights() });
        file_hints[ftt].emplace_back("arkmmd.dll");

        // loveplus
        const std::string loveplus("LovePlus");
        games.push_back(loveplus);
        buttons.insert({ loveplus, loveplus::get_buttons() });
        lights.insert({ loveplus, loveplus::get_lights() });
        file_hints[loveplus].emplace_back("arkklp.dll");

        // scotto
        const std::string scotto("Scotto");
        games.push_back(scotto);
        buttons.insert({ scotto, scotto::get_buttons() });
        lights.insert({ scotto, scotto::get_lights() });
        file_hints[scotto].emplace_back("scotto.dll");

        // drs
        const std::string drs("DANCERUSH");
        games.push_back(drs);
        buttons.insert({ drs, drs::get_buttons() });
        buttons_help.insert({ drs, drs::get_buttons_help() });
        lights.insert({ drs, drs::get_lights() });
        file_hints[drs].emplace_back("superstep.dll");

        // otoca
        const std::string otoca("Otoca D'or");
        games.push_back(otoca);
        buttons.insert({ otoca, otoca::get_buttons() });
        file_hints[otoca].emplace_back("arkkep.dll");

        // winning eleven
        const std::string we("Winning Eleven");
        games.push_back(we);
        buttons.insert({ we, we::get_buttons() });
        analogs.insert({ we, we::get_analogs() });
        lights.insert({ we, we::get_lights() });
        file_hints[we].emplace_back("weac12_bootstrap_release.dll");
        file_hints[we].emplace_back("arknck.dll");

        // silent scope: bone eater
        const std::string silentscope("Silent Scope: Bone Eater");
        games.push_back(silentscope);
        buttons.insert({ silentscope, silentscope::get_buttons() });
        analogs.insert({ silentscope, silentscope::get_analogs() });
        file_hints[silentscope].emplace_back("arkndd.dll");

        // charge machine
        const std::string pcm("Charge Machine");
        games.push_back(pcm);
        buttons.insert({ pcm, pcm::get_buttons() });
        file_hints[pcm].emplace_back("launch.dll");

        // ongaku paradise
        const std::string op("Ongaku Paradise");
        games.push_back(op);
        buttons.insert({ op, onpara::get_buttons() });
        file_hints[op].emplace_back("arkjc9.dll");

        // bc
        const std::string bc("Busou Shinki: Armored Princess Battle Conductor");
        games.push_back(bc);
        buttons.insert({ bc, bc::get_buttons() });
        buttons_help.insert({ bc, bc::get_buttons_help() });
        analogs.insert({ bc, bc::get_analogs() });
        file_hints[bc].emplace_back("game/bsac_app.exe");

        // ccj
        const std::string ccj("Chase Chase Jokers");
        games.push_back(ccj);
        buttons.insert({ ccj, ccj::get_buttons() });
        buttons_help.insert({ ccj, ccj::get_buttons_help() });
        analogs.insert({ ccj, ccj::get_analogs() });
        lights.insert({ ccj, ccj::get_lights() });
        file_hints[ccj].emplace_back("game/chaseproject.exe");

        // QuizKnock STADIUM
        const std::string qks("QuizKnock STADIUM");
        games.push_back(qks);
        buttons.insert({ qks, qks::get_buttons() });
        buttons_help.insert({ qks, qks::get_buttons_help() });
        file_hints[qks].emplace_back("game/uks.exe");

        // Mahjong Fight Girl
        const std::string mfg("Mahjong Fight Girl");
        games.push_back(mfg);
        buttons.insert({ mfg, mfg::get_buttons() });
        buttons_help.insert({ mfg, mfg::get_buttons_help() });
        lights.insert({ mfg, mfg::get_lights() });
        file_hints[mfg].emplace_back("game/MFGClient_Data");

        // Polaris Chord
        const std::string pc("Polaris Chord");
        games.push_back(pc);
        buttons.insert({ pc, pc::get_buttons() });
        buttons_help.insert({ pc, pc::get_buttons_help() });
        analogs.insert({ pc, pc::get_analogs() });
        lights.insert({ pc, pc::get_lights() });
        file_hints[pc].emplace_back("game/svm_Data");
    }

    const std::vector<std::string> &get_games() {
        initialize();

        return games;
    }

    std::vector<Button> *get_buttons(const std::string &game) {
        initialize();
        auto it = buttons.find(game);
        if (it == buttons.end()) {
            return nullptr;
        }
        return &it->second;
    }

    std::string get_buttons_help(const std::string &game) {
        initialize();
        auto it = buttons_help.find(game);
        if (it == buttons_help.end()) {
            return "";
        }
        return it->second;
    }

    std::string get_analogs_help(const std::string &game) {
        initialize();
        auto it = analogs_help.find(game);
        if (it == analogs_help.end()) {
            return "";
        }
        return it->second;
    }

    static std::vector<Button> gen_buttons_keypads(const std::string &game) {
        auto buttons = GameAPI::Buttons::getButtons(game);
        std::vector<std::string> names;
        std::vector<unsigned short> vkey_defaults;

        // loop for 2 keypad units, only setting defaults for keypad 1
        for (size_t unit = 0; unit < 2; unit++) {
            std::string prefix = unit == 0 ? "P1 Keypad " : "P2 Keypad ";

            names.emplace_back(prefix + "0");
            vkey_defaults.push_back(unit == 0 ? VK_NUMPAD0 : 0xFF);

            names.emplace_back(prefix + "1");
            vkey_defaults.push_back(unit == 0 ? VK_NUMPAD1 : 0xFF);

            names.emplace_back(prefix + "2");
            vkey_defaults.push_back(unit == 0 ? VK_NUMPAD2 : 0xFF);

            names.emplace_back(prefix + "3");
            vkey_defaults.push_back(unit == 0 ? VK_NUMPAD3 : 0xFF);

            names.emplace_back(prefix + "4");
            vkey_defaults.push_back(unit == 0 ? VK_NUMPAD4 : 0xFF);

            names.emplace_back(prefix + "5");
            vkey_defaults.push_back(unit == 0 ? VK_NUMPAD5 : 0xFF);

            names.emplace_back(prefix + "6");
            vkey_defaults.push_back(unit == 0 ? VK_NUMPAD6 : 0xFF);

            names.emplace_back(prefix + "7");
            vkey_defaults.push_back(unit == 0 ? VK_NUMPAD7 : 0xFF);

            names.emplace_back(prefix + "8");
            vkey_defaults.push_back(unit == 0 ? VK_NUMPAD8 : 0xFF);

            names.emplace_back(prefix + "9");
            vkey_defaults.push_back(unit == 0 ? VK_NUMPAD9 : 0xFF);

            names.emplace_back(prefix + "00");
            vkey_defaults.push_back(unit == 0 ? VK_RETURN : 0xFF);

            names.emplace_back(prefix + "Decimal");
            vkey_defaults.push_back(unit == 0 ? VK_DECIMAL : 0xFF);

            names.emplace_back(prefix + "Insert Card");
            vkey_defaults.push_back(unit == 0 ? VK_ADD : 0xFF);
        }

        // return sorted buttons
        buttons = GameAPI::Buttons::sortButtons(buttons, names, &vkey_defaults);
        return buttons;
    }

    std::vector<Button> *get_buttons_keypads(const std::string &game) {
        initialize();
        auto it = buttons_keypads.find(game);
        if (it == buttons_keypads.end()) {
            if (game.empty()) {
                return nullptr;
            } else {
                buttons_keypads[game] = gen_buttons_keypads(game);
                return &buttons_keypads[game];
            }
        }
        return &it->second;
    }

    static std::vector<Button> gen_buttons_overlay(const std::string &game) {

        // get buttons
        auto buttons = GameAPI::Buttons::getButtons(game);
        std::vector<std::string> names;
        std::vector<unsigned short> vkey_defaults;

        // overlay button definitions
        names.emplace_back("Screenshot");
        vkey_defaults.push_back(VK_SNAPSHOT);
        names.emplace_back("Toggle Main Menu");
        vkey_defaults.push_back(VK_ESCAPE);
        names.emplace_back("Toggle Sub Screen");
        vkey_defaults.push_back(VK_PRIOR);
        names.emplace_back("Insert Coin");
        vkey_defaults.push_back(VK_F1);
        names.emplace_back("Toggle IO Panel");
        vkey_defaults.push_back(VK_F2);
        names.emplace_back("Toggle Config");
        vkey_defaults.push_back(VK_F4);
        names.emplace_back("Toggle Virtual Keypad P1");
        vkey_defaults.push_back(VK_F5);
        names.emplace_back("Toggle Virtual Keypad P2");
        vkey_defaults.push_back(VK_F6);
        names.emplace_back("Toggle Card Manager");
        vkey_defaults.push_back(VK_F7);
        names.emplace_back("Toggle Log");
        vkey_defaults.push_back(VK_F8);
        names.emplace_back("Toggle Control");
        vkey_defaults.push_back(VK_F9);
        names.emplace_back("Toggle Patch Manager");
        vkey_defaults.push_back(VK_F10);
        names.emplace_back("Toggle Screen Resize");
        vkey_defaults.push_back(VK_F11);
        names.emplace_back("Toggle Overlay");
        vkey_defaults.push_back(VK_F12);
        names.emplace_back("Toggle Camera Control");
        vkey_defaults.push_back(0xFF);
        names.emplace_back("Player 1 PIN Macro");
        vkey_defaults.push_back(0xFF);
        names.emplace_back("Player 2 PIN Macro");
        vkey_defaults.push_back(0xFF);
        names.emplace_back("Screen Resize");
        vkey_defaults.push_back(0xFF);
        names.emplace_back("Screen Resize Scene 1");
        vkey_defaults.push_back(0xFF);
        names.emplace_back("Screen Resize Scene 2");
        vkey_defaults.push_back(0xFF);
        names.emplace_back("Screen Resize Scene 3");
        vkey_defaults.push_back(0xFF);
        names.emplace_back("Screen Resize Scene 4");
        vkey_defaults.push_back(0xFF);
        names.emplace_back("Force Exit Game");
        vkey_defaults.push_back(0xFF);
        names.emplace_back("Hotkey Enable 1");
        vkey_defaults.push_back(0xFF);
        names.emplace_back("Hotkey Enable 2");
        vkey_defaults.push_back(0xFF);
        names.emplace_back("Hotkey Toggle");
        vkey_defaults.push_back(0xFF);

        // return sorted buttons
        buttons = GameAPI::Buttons::sortButtons(buttons, names, &vkey_defaults);
        return buttons;
    }

    std::vector<Button> *get_buttons_overlay(const std::string &game) {
        initialize();
        auto it = buttons_overlay.find(game);
        if (it == buttons_overlay.end()) {
            if (game.empty()) {
                return nullptr;
            } else {
                buttons_overlay[game] = gen_buttons_overlay(game);
                return &buttons_overlay[game];
            }
        }
        return &it->second;
    }

    std::vector<Analog> *get_analogs(const std::string &game) {
        initialize();
        auto it = analogs.find(game);
        if (it == analogs.end()) {
            return nullptr;
        }
        return &it->second;
    }

    std::vector<Light> *get_lights(const std::string &game) {
        initialize();
        auto it = lights.find(game);
        if (it == lights.end()) {
            return nullptr;
        }
        return &it->second;
    }

    static std::vector<Option> gen_options(const std::string &game) {

        // get options
        auto options = GameAPI::Options::getOptions(game);

        // sort options
        GameAPI::Options::sortOptions(options, launcher::get_option_definitions());

        // merge options
        auto merged = launcher::merge_options(options, *LAUNCHER_OPTIONS);

        // return result
        return merged;
    }

    std::vector<Option> *get_options(const std::string &game) {
        initialize();

        if (game.empty()) {
            return LAUNCHER_OPTIONS.get();
        }

        auto it = options.find(game);
        if (it == options.end()) {
            options[game] = gen_options(game);
            return &options[game];
        }

        return &it->second;
    }

    std::vector<std::string> *get_game_file_hints(const std::string &game) {
        initialize();
        auto it = file_hints.find(game);
        if (it == file_hints.end()) {
            return nullptr;
        }
        return &it->second;
    }
}
