/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Tests for the Magnification API
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira <gabrielcarvfer@gmail.com>
 */

/* NVDA 2026 loads magnification.dll when it starts (its screen curtain) */

#include <apitest.h>
#include <magnification.h>

START_TEST(magnification)
{
    MAGCOLOREFFECT Effect;
    float Level = 0.0f;
    int x = -1, y = -1, i, j;
    BOOL Identity = TRUE;

    ok(MagInitialize(), "MagInitialize failed: %lu\n", GetLastError());

    ok(MagGetFullscreenTransform(&Level, &x, &y), "MagGetFullscreenTransform failed: %lu\n", GetLastError());
    ok(Level == 1.0f && x == 0 && y == 0, "Fullscreen transform %f (%d,%d)\n", Level, x, y);

    memset(&Effect, 0xcc, sizeof(Effect));
    ok(MagGetFullscreenColorEffect(&Effect), "MagGetFullscreenColorEffect failed: %lu\n", GetLastError());
    for (i = 0; i < 5; i++)
        for (j = 0; j < 5; j++)
            if (Effect.transform[i][j] != (i == j ? 1.0f : 0.0f))
                Identity = FALSE;
    ok(Identity, "The fullscreen color effect isn't the identity\n");

    /* Setting what is already set */
    ok(MagSetFullscreenTransform(1.0f, 0, 0), "MagSetFullscreenTransform failed: %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    ok(!MagSetFullscreenTransform(0.5f, 0, 0), "A magnification below 1 was accepted\n");

    ok(MagUninitialize(), "MagUninitialize failed: %lu\n", GetLastError());
}
