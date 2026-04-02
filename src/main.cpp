#include <Geode/Bindings.hpp>
#include <Geode/Geode.hpp>
#include <Geode/loader/ModEvent.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/utils/string.hpp>

#include <array>
#include <algorithm>
#include <filesystem>
#include <random>
#include <ranges>
#include <string_view>
#include <vector>

using namespace geode::prelude;

namespace {
    struct TierSpec final {
        int fps;
        int weight;
        cocos2d::ccColor3B color;
        char const* settingKey;
        char const* soundFile;
        float soundSpeed;
        float soundVolume;
    };

    constexpr float kBaseLabelScale = 0.35f;
    constexpr float kCornerInset = 12.0f;
    constexpr float kLabelGap = 4.0f;
    constexpr float kSoggyCatHoldTime = 0.08f;
    constexpr float kSoggyCatFadeTime = 0.55f;
    constexpr size_t kTierCount = 5;
    constexpr char kSoggyCatFile[] = "soggy-cat.jpg";

    const std::array<TierSpec, kTierCount> kTiers = {{
        { 60, 60, { 85, 235, 110 }, "show-60", "frameperfect-ding.mp3", 1.00f, 0.55f },
        { 120, 25, { 170, 225, 70 }, "show-120", "frameperfect-ding.mp3", 0.89f, 0.62f },
        { 240, 10, { 255, 191, 60 }, "show-240", "frameperfect-ding.mp3", 0.77f, 0.70f },
        { 360, 4, { 220, 105, 55 }, "show-360", "frameperfect-ding.mp3", 0.63f, 0.80f },
        { 999, 1, { 110, 20, 20 }, "show-999", "frameperfect-999-bell.mp3", 0.40f, 0.98f },
    }};

    enum class LabelCorner {
        TopLeft,
        TopRight,
        BottomLeft,
        BottomRight,
    };

    bool g_isCorrectingTierSetting = false;

    bool isTierSettingKey(std::string_view key) {
        return std::ranges::any_of(kTiers, [key](TierSpec const& tier) {
            return key == tier.settingKey;
        });
    }

    bool isOverlaySettingKey(std::string_view key) {
        return key == "enabled" || key == "label-position" || key == "label-size" || isTierSettingKey(key);
    }

    bool isModEnabled() {
        return Mod::get()->getSettingValue<bool>("enabled");
    }

    int labelSizeMultiplier() {
        return std::clamp(Mod::get()->getSettingValue<int>("label-size"), 1, 3);
    }

    float labelScale() {
        return kBaseLabelScale * static_cast<float>(labelSizeMultiplier());
    }

    LabelCorner getLabelCorner() {
        auto const setting = Mod::get()->getSettingValue<std::string_view>("label-position");
        if (setting == "top-left") {
            return LabelCorner::TopLeft;
        }
        if (setting == "bottom-left") {
            return LabelCorner::BottomLeft;
        }
        if (setting == "bottom-right") {
            return LabelCorner::BottomRight;
        }
        return LabelCorner::TopRight;
    }

    double procChance() {
        auto const setting = Mod::get()->getSettingValue<std::string_view>("proc-rate");
        if (setting == "rare") {
            return 0.20;
        }
        if (setting == "every-press") {
            return 1.0;
        }
        return 0.40;
    }

    bool isTierEnabled(size_t index) {
        return Mod::get()->getSettingValue<bool>(kTiers.at(index).settingKey);
    }

    int enabledTierCount() {
        int enabled = 0;
        for (size_t i = 0; i < kTiers.size(); ++i) {
            enabled += isTierEnabled(i) ? 1 : 0;
        }
        return enabled;
    }

    std::vector<size_t> getEnabledTierIndices() {
        std::vector<size_t> result;
        result.reserve(kTiers.size());
        for (size_t i = 0; i < kTiers.size(); ++i) {
            if (isTierEnabled(i)) {
                result.push_back(i);
            }
        }
        return result;
    }

    std::string labelText(size_t index, int count) {
        return fmt::format("{} FPS: {}", kTiers.at(index).fps, count);
    }

    std::filesystem::path resolveResourcePath(char const* subdirectory, char const* fileName) {
        auto const baseDir = Mod::get()->getResourcesDir();
        auto const directPath = baseDir / fileName;
        if (std::filesystem::exists(directPath)) {
            return directPath;
        }
        return baseDir / subdirectory / fileName;
    }

    std::filesystem::path resolveSoundPath(size_t index) {
        return resolveResourcePath("sounds", kTiers.at(index).soundFile);
    }

    std::filesystem::path resolveSoggyCatPath() {
        return resolveResourcePath("images", kSoggyCatFile);
    }

    void playTierSound(size_t index) {
        auto const path = resolveSoundPath(index);
        if (!std::filesystem::exists(path)) {
            log::warn("Missing frame perfect sound '{}'", kTiers.at(index).soundFile);
            return;
        }

        auto* audio = FMODAudioEngine::sharedEngine();
        if (!audio) {
            return;
        }

        audio->playEffect(
            utils::string::pathToString(path),
            kTiers.at(index).soundSpeed,
            0.0f,
            kTiers.at(index).soundVolume
        );
    }

    size_t pickWeightedTier(std::vector<size_t> const& enabledTiers, std::mt19937& rng) {
        int totalWeight = 0;
        for (auto const index : enabledTiers) {
            totalWeight += kTiers.at(index).weight;
        }

        std::uniform_int_distribution<int> distribution(1, std::max(1, totalWeight));
        int roll = distribution(rng);
        for (auto const index : enabledTiers) {
            roll -= kTiers.at(index).weight;
            if (roll <= 0) {
                return index;
            }
        }

        return enabledTiers.front();
    }

    bool shouldProc(std::mt19937& rng) {
        auto const chance = procChance();
        if (chance >= 1.0) {
            return true;
        }

        std::bernoulli_distribution distribution(chance);
        return distribution(rng);
    }

    void ensureAtLeastOneTierEnabled() {
        if (enabledTierCount() > 0) {
            return;
        }

        g_isCorrectingTierSetting = true;
        Mod::get()->setSettingValue<bool>("show-60", true);
        g_isCorrectingTierSetting = false;
    }

    void handleTierSettingCorrection(std::string_view key) {
        if (!isTierSettingKey(key) || g_isCorrectingTierSetting) {
            return;
        }

        if (enabledTierCount() > 0) {
            return;
        }

        g_isCorrectingTierSetting = true;
        Mod::get()->setSettingValue<bool>(key, true);
        g_isCorrectingTierSetting = false;
    }
}

class $modify(FramePerfectPlayLayer, PlayLayer) {
    struct Fields {
        std::array<int, kTierCount> counts {};
        std::array<CCLabelBMFont*, kTierCount> labels {};
        CCNode* overlay = nullptr;
        CCSprite* soggyCatFlash = nullptr;
        std::mt19937 rng { std::random_device{}() };
    };

    void clearOverlay() {
        if (m_fields->soggyCatFlash) {
            m_fields->soggyCatFlash->stopAllActions();
            m_fields->soggyCatFlash = nullptr;
        }
        if (m_fields->overlay) {
            m_fields->overlay->removeFromParentAndCleanup(true);
            m_fields->overlay = nullptr;
        }
        m_fields->labels.fill(nullptr);
    }

    void createSoggyCatFlash() {
        m_fields->soggyCatFlash = nullptr;
        if (!m_fields->overlay) {
            return;
        }

        auto const path = resolveSoggyCatPath();
        if (!std::filesystem::exists(path)) {
            log::warn("Missing soggy cat image '{}'", kSoggyCatFile);
            return;
        }

        auto const pathString = utils::string::pathToString(path);
        auto* texture = CCTextureCache::sharedTextureCache()->addImage(pathString.c_str(), false);
        if (!texture) {
            log::warn("Failed to load soggy cat image '{}'", pathString);
            return;
        }

        auto* sprite = CCSprite::createWithTexture(texture);
        if (!sprite) {
            log::warn("Failed to create soggy cat sprite");
            return;
        }

        auto const winSize = CCDirector::sharedDirector()->getWinSize();
        auto const contentSize = sprite->getContentSize();
        if (contentSize.width <= 0.0f || contentSize.height <= 0.0f) {
            log::warn("Soggy cat image has invalid size");
            return;
        }

        sprite->setAnchorPoint({ 0.5f, 0.5f });
        sprite->setPosition({ winSize.width * 0.5f, winSize.height * 0.5f });
        sprite->setScaleX(winSize.width / contentSize.width);
        sprite->setScaleY(winSize.height / contentSize.height);
        sprite->setOpacity(0);
        sprite->setID("soggy-cat-flash"_spr);

        m_fields->overlay->addChild(sprite, 10000);
        m_fields->soggyCatFlash = sprite;
    }

    void showSoggyCatFlash() {
        auto* sprite = m_fields->soggyCatFlash;
        if (!sprite) {
            return;
        }

        sprite->stopAllActions();
        sprite->setOpacity(255);
        sprite->runAction(CCSequence::create(
            CCDelayTime::create(kSoggyCatHoldTime),
            CCFadeOut::create(kSoggyCatFadeTime),
            nullptr
        ));
    }

    void refreshLabelValues() {
        for (size_t i = 0; i < kTiers.size(); ++i) {
            if (auto* label = m_fields->labels.at(i)) {
                label->setString(labelText(i, m_fields->counts.at(i)).c_str());
            }
        }
    }

    void resetCounterState() {
        m_fields->counts.fill(0);
        refreshLabelValues();
    }

public:
    void rebuildOverlay() {
        clearOverlay();

        if (!isModEnabled() || !m_uiLayer) {
            return;
        }

        auto const enabledTiers = getEnabledTierIndices();
        if (enabledTiers.empty()) {
            return;
        }

        auto const winSize = CCDirector::sharedDirector()->getWinSize();
        auto* overlay = CCNode::create();
        overlay->setContentSize(winSize);
        overlay->setAnchorPoint({ 0.0f, 0.0f });
        overlay->setPosition({ 0.0f, 0.0f });
        overlay->setID("frameperfect-overlay"_spr);
        m_uiLayer->addChild(overlay);
        m_fields->overlay = overlay;
        createSoggyCatFlash();

        auto const corner = getLabelCorner();
        auto const topAligned = corner == LabelCorner::TopLeft || corner == LabelCorner::TopRight;
        auto const rightAligned = corner == LabelCorner::TopRight || corner == LabelCorner::BottomRight;
        auto const scale = labelScale();

        float cursorX = rightAligned ? winSize.width - kCornerInset : kCornerInset;
        float cursorY = topAligned ? winSize.height - kCornerInset : kCornerInset;

        for (auto const tierIndex : enabledTiers) {
            auto* label = CCLabelBMFont::create(
                labelText(tierIndex, m_fields->counts.at(tierIndex)).c_str(),
                "bigFont.fnt"
            );
            label->setColor(kTiers.at(tierIndex).color);
            label->setOpacity(225);
            label->setAnchorPoint({ rightAligned ? 1.0f : 0.0f, topAligned ? 1.0f : 0.0f });
            label->setScale(scale);
            label->setPosition({ cursorX, cursorY });

            overlay->addChild(label);
            m_fields->labels.at(tierIndex) = label;

            float const step = label->getContentSize().height * scale + kLabelGap;
            cursorY += topAligned ? -step : step;
        }
    }

    void pulseTierLabel(size_t index) {
        auto* label = m_fields->labels.at(index);
        if (!label) {
            return;
        }

        float const baseScale = labelScale();
        label->stopAllActions();
        label->setScale(baseScale);
        label->runAction(CCSequence::create(
            CCEaseSineOut::create(CCScaleTo::create(0.08f, baseScale * 1.25f)),
            CCEaseSineInOut::create(CCScaleTo::create(0.14f, baseScale)),
            nullptr
        ));
    }

    void recordFakeFramePerfect() {
        if (!isModEnabled() || m_isPaused || m_hasCompletedLevel) {
            return;
        }

        auto const enabledTiers = getEnabledTierIndices();
        if (enabledTiers.empty() || !shouldProc(m_fields->rng)) {
            return;
        }

        auto const pickedTier = pickWeightedTier(enabledTiers, m_fields->rng);
        ++m_fields->counts.at(pickedTier);
        refreshLabelValues();
        pulseTierLabel(pickedTier);
        playTierSound(pickedTier);
        if (kTiers.at(pickedTier).fps == 999) {
            showSoggyCatFlash();
        }
    }

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) {
            return false;
        }

        resetCounterState();
        return true;
    }

    void setupHasCompleted() {
        PlayLayer::setupHasCompleted();
        resetCounterState();
        rebuildOverlay();
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        resetCounterState();
        rebuildOverlay();
    }

    void resetLevelFromStart() {
        PlayLayer::resetLevelFromStart();
        resetCounterState();
        rebuildOverlay();
    }

    void loadFromCheckpoint(CheckpointObject* checkpoint) {
        PlayLayer::loadFromCheckpoint(checkpoint);
        resetCounterState();
        rebuildOverlay();
    }

    void onQuit() {
        clearOverlay();
        PlayLayer::onQuit();
    }
};

namespace {
    void refreshActiveOverlay() {
        auto* playLayer = PlayLayer::get();
        if (!playLayer) {
            return;
        }

        if (auto* modified = geode::cast::modify_cast<FramePerfectPlayLayer*>(playLayer)) {
            modified->rebuildOverlay();
        }
    }
}

class $modify(FramePerfectGameLayer, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool isPlayer1) {
        auto* playLayer = PlayLayer::get();
        bool const shouldRecord = down &&
            button == 1 &&
            playLayer &&
            static_cast<GJBaseGameLayer*>(playLayer) == static_cast<GJBaseGameLayer*>(this) &&
            !playLayer->m_isPaused &&
            !playLayer->m_hasCompletedLevel;

        GJBaseGameLayer::handleButton(down, button, isPlayer1);

        if (!shouldRecord) {
            return;
        }

        if (auto* modified = geode::cast::modify_cast<FramePerfectPlayLayer*>(playLayer)) {
            modified->recordFakeFramePerfect();
        }
    }
};

$on_mod(Loaded) {
    ensureAtLeastOneTierEnabled();

    listenForAllSettingChanges([](std::string_view key, std::shared_ptr<SettingV3>) {
        handleTierSettingCorrection(key);

        if (isOverlaySettingKey(key)) {
            ensureAtLeastOneTierEnabled();
            refreshActiveOverlay();
        }
    });
}
