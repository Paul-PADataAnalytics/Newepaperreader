#pragma once

#include "Application.h"

// Games hub: a list of the games; each opens as its own app and returns
// here when closed.
class GamesApp : public Application {
public:
    void onCreate() override {}
    void onDestroy() override {}
    void draw() override;
    void handleTouch(int x, int y) override;
};
