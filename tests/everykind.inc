// A song with every effect there was when main wrote it (up to DUCK): in the tracks' slots (in number order), on the master and on the buses, none at its usual settings.
// (A newer effect is not put in: the file this is held to is not remade. testSongFilesAreAsTheyWere checks each newer one by its own word.)
const int kKindsMainWrote = static_cast<int>(EffectKind::Duck) + 1;
static void fillWithEveryKind(Project& p) {
    for (int t = 0; t < 4; ++t) p.tracks[t].kind = TrackKind::Drums;
    for (int k = 1; k < kKindsMainWrote; ++k) {
        EffectData& d = p.tracks[(k - 1) / kTrackSlots].effects[(k - 1) % kTrackSlots];
        d.kind = k;
        for (int i = 0; i < effectInfo(static_cast<EffectKind>(k)).params; ++i) d.param[i] = 0.07f * static_cast<float>(k) + 0.031f * static_cast<float>(i);
    }
    p.masterEffects[0].kind = static_cast<int>(EffectKind::Hall);
    p.masterEffects[1].kind = static_cast<int>(EffectKind::Tape);
    for (int i = 0; i < kEffectParams; ++i) p.masterEffects[0].param[i] = 0.2f + 0.1f * static_cast<float>(i), p.masterEffects[1].param[i] = 0.9f - 0.1f * static_cast<float>(i);
    p.busEffects[0].kind = static_cast<int>(EffectKind::Delay);
    p.busEffects[1].kind = static_cast<int>(EffectKind::Reverb);
    for (int i = 0; i < kEffectParams; ++i) p.busEffects[0].param[i] = 0.15f + 0.2f * static_cast<float>(i), p.busEffects[1].param[i] = i < 3 ? 0.35f + 0.2f * static_cast<float>(i) : 0.f;
    p.tracks[0].send[0] = 0.35f;
    p.tracks[2].send[1] = 0.6f;
    p.tracks[1].automation[0][autoLaneOfEffect(2, 1)][3] = 0.25f;
}
