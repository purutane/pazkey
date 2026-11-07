#include "fcitx_config.h"

namespace pazkey {

void toFcitxConfig(const Config &c, PazkeyFcitxConfig &to) {
    CandidatesGroup candidates;
    candidates.pageSize.setValue(c.candidates.pageSize);
    candidates.selectWithDigits.setValue(c.candidates.selectWithDigits);
    to.candidates.setValue(candidates);

    InputGroup input;
    input.liveConversion.setValue(c.liveConversion.enabled);
    to.input.setValue(input);

    ContextGroup context;
    context.enabled.setValue(c.context.enabled);
    context.maxChars.setValue(c.context.maxChars);
    to.context.setValue(context);

    ZenzaiGroup zenzai;
    zenzai.enabled.setValue(c.zenzai.enabled);
    zenzai.modelPath.setValue(c.zenzai.modelPath);
    zenzai.inferenceLimit.setValue(c.zenzai.inferenceLimit);
    to.zenzai.setValue(zenzai);

    LearningGroup learning;
    learning.enabled.setValue(c.learning.enabled);
    to.learning.setValue(learning);

    AdvancedGroup advanced;
    advanced.maxPages.setValue(c.candidates.maxPages);
    advanced.fullMs.setValue(c.timeout.fullMs);
    advanced.saveEveryCommits.setValue(c.learning.saveEveryCommits);
    advanced.saveIdleSeconds.setValue(c.learning.saveIdleSeconds);
    to.advanced.setValue(advanced);
}

void setZenzaiStatus(PazkeyFcitxConfig &to, const std::string &status) {
    ZenzaiGroup zenzai = *to.zenzai;
    zenzai.status.setValue(status);
    fcitx_config_detail::currentStatusText() = status;
    to.zenzai.setValue(zenzai);
}

Config fromFcitxConfig(const PazkeyFcitxConfig &from, Config c) {
    c.candidates.pageSize = from.candidates->pageSize.value();
    c.candidates.selectWithDigits = from.candidates->selectWithDigits.value();
    c.liveConversion.enabled = from.input->liveConversion.value();
    c.context.enabled = from.context->enabled.value();
    c.context.maxChars = from.context->maxChars.value();
    c.zenzai.enabled = from.zenzai->enabled.value();
    c.zenzai.modelPath = from.zenzai->modelPath.value();
    c.zenzai.inferenceLimit = from.zenzai->inferenceLimit.value();
    c.learning.enabled = from.learning->enabled.value();
    c.candidates.maxPages = from.advanced->maxPages.value();
    c.timeout.fullMs = from.advanced->fullMs.value();
    c.learning.saveEveryCommits = from.advanced->saveEveryCommits.value();
    c.learning.saveIdleSeconds = from.advanced->saveIdleSeconds.value();
    return c;
}

}  // namespace pazkey
