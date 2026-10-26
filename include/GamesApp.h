#pragma once

#include "Application.h"

// Games hub: lists the games; leaving a game returns here.
class GamesApp : public Application {
public:
    void onCreate() override {}
    void onDestroy() override {}
    void draw() override;
    void handleTouch(int x, int y) override;
};
