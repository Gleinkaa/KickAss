#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "KickEngine.h"
#include "EnvCurve.h"
#include <atomic>

class PresetManager;  // fwd

//==============================================================================
class KickAssProcessor : public juce::AudioProcessor
{
public:
    KickAssProcessor();
    ~KickAssProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi()  const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 1.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // Undo/Redo. MUST be declared BEFORE apvts so it is constructed first — the
    // apvts ctor takes &undoManager, so the UndoManager has to already exist.
    juce::UndoManager undoManager;

    juce::AudioProcessorValueTreeState apvts;

    //==============================================================================
    // Undo/Redo public API. APVTS parameter edits are pushed onto the UndoManager
    // automatically (it owns one); the editor opens a new transaction per gesture
    // so one drag = one undo step. NOTE: freehand breakpoint-curve edits live in
    // apvts.state (NOT as APVTS params) and are NOT captured by the UndoManager —
    // see restoreVolCurveFromStateOrAhdsr() callers below.
    juce::UndoManager& getUndoManager() noexcept { return undoManager; }
    void undo();
    void redo();
    bool canUndo() const;
    bool canRedo() const;

    /** UI-thread offline render for the visualizer (Phase 4). */
    void offlineRender (juce::AudioBuffer<float>& buffer, double durationMs);

    /** UI-thread offline render of ONLY the transient/sample layer (body muted).
        Backs the visualizer's TRANSIENT view so the bare click/sample shape is
        visible. Same fixed 48 kHz + dedicated offline engine as offlineRender. */
    void offlineRenderTransient (juce::AudioBuffer<float>& buffer, double durationMs);

    /** Offline-render the current patch to a 24-bit stereo WAV at `dest`
        (48 kHz, matching offlineRender). Used by EXPORT WAV and the v1.1
        drag-out affordance, and exercised headlessly by Tests/RenderWavTest.cpp.
        Creates parent dirs as needed. Returns false on render/IO failure. */
    bool renderToWavFile (const juce::File& dest, double durationMs);

    //==============================================================================
    // v1.1 — drag-a-WAV transient layer.
    //
    // loadTransientSampleFile: UI thread. Reads the audio file (any format
    //   registered by AudioFormatManager — wav/aif/flac/…), sums to mono, and
    //   publishes it to BOTH the realtime engine and the offline (visualizer)
    //   engine via their lock-free double-buffer. Stores the file PATH in
    //   apvts.state under <Sample id="transient" path="..."> for persistence.
    //   Returns false if the file can't be read.
    //
    // Persistence is PATH-BASED only: setStateInformation reloads from the stored
    //   path if the file is still readable; a missing file leaves the slot empty
    //   (no crash). Embedding the raw audio in the plugin state is deferred.
    bool loadTransientSampleFile (const juce::File& file);

    /** UI thread. Clears the sample from both engines and removes the state node. */
    void clearTransientSample();

    /** Stored transient-sample path ("" if none). Reads apvts.state. */
    juce::String getTransientSamplePath() const;

    /** B3/B4: make BOTH engines match the <Sample> node currently in
        apvts.state. If the node carries a path to an existing, readable file,
        the sample is (re)loaded; otherwise (no node, empty path, missing or
        unreadable file) the engines are cleared WITHOUT deleting the node —
        a project opened on a machine where the file is absent must not
        destroy the path for sessions where it exists. Called after every
        path that replaces APVTS state: setStateInformation,
        PresetManager::loadKickPreset, KickAssEditor::doToggleAB. */
    void restoreTransientSampleFromState();

    /** B9: strip <PARAM> children whose 'value' attribute parses to a
        non-finite number (NaN / ±Inf — e.g. value="nan"). JUCE's XML reader
        keeps such attributes as strings and the var→double conversion
        happily yields NaN, which APVTS then pushes straight into the
        parameter (NaN audio). Removing the child makes the parameter fall
        back to its default instead (APVTS re-creates the child without a
        value property and applies the default). Call BEFORE
        apvts.replaceState(). */
    void removeNonFiniteParameterChildren (juce::ValueTree& stateTree);

    /** B7: undo-preserving state swap for A/B compare.
        AudioProcessorValueTreeState::replaceState() calls
        undoManager->clearUndoHistory(), so the A/B toggle used to wipe the
        undo stack on every press. This applies a snapshot state (e.g. an A/B
        slot from copyState()) to the LIVE APVTS inside ONE UndoManager
        transaction instead — the swap itself becomes a single undo step and
        edits made before the toggle stay undoable:
        - every parameter reads its value out of newState's <PARAM> child
          (matching APVTS's own storage: the "value" property holds the
          DENORMALISED value, verified against copyState() output) and is
          pushed via setValueNotifyingHost; a parameter absent from newState
          goes to its default;
        - the non-PARAM children the A/B slots swap, <Curves> and <Sample>,
          are removed/re-appended as undoable child operations (a copy, never
          the slot's own tree, so the live state can't alias a stored slot);
        - restoreVolCurveFromStateOrAhdsr() / restoreTransientSampleFromState()
          then make the engines match, exactly like setStateInformation.
        Message-thread only (like every setValueNotifyingHost caller). Never
        touches the undo history itself — no clearUndoHistory(). */
    void applyStatePreservingUndo (const juce::ValueTree& newState);

    /** UI reads this at ~30Hz to draw the playback cursor in the waveform display. */
    int getPlaybackSamplePos() const noexcept { return engine.getPlaybackSamplePos(); }

    /** Thread-safe trigger request. UI thread stores note+velocity here; the audio
        thread drains it at the start of processBlock and calls engine.triggerNote()
        where it is contractually safe. Never call engine.triggerNote() from the UI. */
    void requestTrigger (int midiNote = 60, float velocity = 1.0f) noexcept;

    KickEngine& getEngine() noexcept { return engine; }
    PresetManager& getPresetManager() noexcept { return *presetManager; }

    //==============================================================================
    // Phase 6b — breakpoint envelope storage.
    // The editor mutates this directly; call syncVolCurveToValueTree() afterwards
    // so the curve is persisted in apvts.state for save/load. Audio thread takes
    // its own snapshot at noteOn via getVolEnvCurveSnapshot() under the spin-lock.
    EnvCurve& getVolEnvCurve()                  noexcept { return volEnvCurve; }
    const EnvCurve& getVolEnvCurve() const      noexcept { return volEnvCurve; }
    juce::SpinLock& getVolCurveLock()           noexcept { return volCurveLock; }

    /** Editor calls this after any edit to persist the curve into apvts.state. */
    void syncVolCurveToValueTree();

    /** Build the curve from the current AHDSR APVTS values; used on first run + Simple→Advanced. */
    void rebuildVolCurveFromAhdsr();

    /** Try to load curve from apvts.state <Curves>/vol_env, falling back to AHDSR rebuild.
        Called after any path that mutates APVTS state (preset load, replaceState, factory apply). */
    void restoreVolCurveFromStateOrAhdsr();

    /** True iff envelope_mode = Advanced (uses custom curve), false = Simple (uses AHDSR knobs). */
    bool isEnvelopeAdvanced() const noexcept;

    /** Publish the current volEnvCurve to both audio and offline engines (lock-free flip). */
    void publishVolCurveToEngines();

    // Curve-source bookkeeping. "ahdsr" = derived from current knob values, refreshable.
    // "custom" = user-edited via BreakpointEditor, do not auto-overwrite.
    juce::String getVolCurveMode() const;            // returns "ahdsr" if missing
    void setVolCurveMode (const juce::String& mode); // "ahdsr" | "custom"

    /** Called when the user enters Advanced mode. If the curve was never customized,
        refresh it from current AHDSR knob values so the displayed Advanced curve
        matches what was just heard in Simple mode. No-op if mode is already "custom". */
    void onEnterAdvancedMode();

private:
    //==============================================================================
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    KickEngine engine;
    KickEngine offlineEngine;   // dedicated for UI-thread visualizer renders (decoupled from RT state)
    std::unique_ptr<PresetManager> presetManager;

    // Cached atomic pointers — read once, avoid the lookup on the audio thread.
    void cacheParamPointers();
    void readParamsIntoEngine();

    // Shared body for offlineRender / offlineRenderTransient. Builds KickParams
    // from the current APVTS values; `soloTransient` mutes the synth body.
    void offlineRenderImpl (juce::AudioBuffer<float>& buffer, double durationMs, bool soloTransient);

    // PITCH
    std::atomic<float>* pStartFreq    = nullptr;
    std::atomic<float>* pMidFreq      = nullptr;
    std::atomic<float>* pEndFreq      = nullptr;
    std::atomic<float>* pSweepTime1   = nullptr;
    std::atomic<float>* pSweepTime2   = nullptr;
    std::atomic<float>* pPitchCurve   = nullptr;
    // AMP
    std::atomic<float>* pVolAttack    = nullptr;
    std::atomic<float>* pVolHold      = nullptr;
    std::atomic<float>* pVolDecay1    = nullptr;
    std::atomic<float>* pVolSustain   = nullptr;
    std::atomic<float>* pVolDecay2    = nullptr;
    std::atomic<float>* pVolCurve     = nullptr;
    // SCOOP
    std::atomic<float>* pScoopStart   = nullptr;
    std::atomic<float>* pScoopLength  = nullptr;
    std::atomic<float>* pScoopDepth   = nullptr;
    // TRANSIENT
    std::atomic<float>* pClickVol     = nullptr;
    std::atomic<float>* pClickType    = nullptr;   // choice param backs onto float
    std::atomic<float>* pClickHpf     = nullptr;
    std::atomic<float>* pClickTone    = nullptr;
    std::atomic<float>* pClickDecay   = nullptr;
    // DRIVE
    std::atomic<float>* pDrive        = nullptr;
    std::atomic<float>* pTailDrive    = nullptr;
    std::atomic<float>* pSatType      = nullptr;   // choice param backs onto float
    // MASTER
    std::atomic<float>* pInvertPhase  = nullptr;   // bool backs onto float
    std::atomic<float>* pSafetyLimit  = nullptr;   // bool backs onto float
    std::atomic<float>* pOutputGain   = nullptr;
    std::atomic<float>* pPitchTrack   = nullptr;
    std::atomic<float>* pPhaseOffset  = nullptr;
    // GLOBAL
    std::atomic<float>* pEnvelopeMode = nullptr;   // 0 = Simple, 1 = Advanced

    // Phase 6b — breakpoint envelope storage (UI-thread authoritative)
    EnvCurve       volEnvCurve;
    juce::SpinLock volCurveLock;

    // UI→audio trigger queue (lock-free single-slot).
    // pendingNote stores the MIDI note; pendingVelocity stores velocity.
    // A pendingNote value of -1 means "no pending trigger".
    std::atomic<int>   pendingNote     { -1 };
    std::atomic<float> pendingVelocity { 1.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KickAssProcessor)
};
