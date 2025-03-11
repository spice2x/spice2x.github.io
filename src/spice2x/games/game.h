#pragma once

#include <string>

namespace games {

    class Game {
    private:
        std::string name;

    public:

        Game(std::string name);
        virtual ~Game() = default;

        // where the main magic will happen
        virtual void attach();

        // optional
        virtual void pre_attach();
        virtual void post_attach();
        virtual void detach();
    };
}
