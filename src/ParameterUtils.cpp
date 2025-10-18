#include "ParameterUtils.h"

bool ParameterUtils::isValidParameterIndex(SX_Instance* instance, int index, int maxActiveParams)
{
    return instance != nullptr && index >= 0 && index < maxActiveParams;
}

double ParameterUtils::normalizedToActualValue(SX_Instance* instance, int paramIndex, float normalizedValue)
{
    if (!instance)
        return 0.0;

    double minVal, maxVal, step;
    JesusonicAPI.sx_getParmVal(instance, paramIndex, &minVal, &maxVal, &step);

    return minVal + normalizedValue * (maxVal - minVal);
}

float ParameterUtils::actualToNormalizedValue(SX_Instance* instance, int paramIndex, double actualValue)
{
    if (!instance)
        return 0.0f;

    double minVal, maxVal, step;
    JesusonicAPI.sx_getParmVal(instance, paramIndex, &minVal, &maxVal, &step);

    if (maxVal > minVal)
        return static_cast<float>((actualValue - minVal) / (maxVal - minVal));

    return 0.0f;
}

bool ParameterUtils::getParameterRange(
    SX_Instance* instance,
    int paramIndex,
    double& minVal,
    double& maxVal,
    double& step
)
{
    if (!instance)
    {
        minVal = maxVal = step = 0.0;
        return false;
    }

    double currentVal = JesusonicAPI.sx_getParmVal(instance, paramIndex, &minVal, &maxVal, &step);
    juce::ignoreUnused(currentVal);
    return true;
}

ParameterUtils::ParameterType ParameterUtils::detectParameterType(SX_Instance* instance, int paramIndex)
{
    if (!instance)
        return ParameterType::Float;

    double minVal, maxVal, step;
    if (!getParameterRange(instance, paramIndex, minVal, maxVal, step))
        return ParameterType::Float;

    bool isEnum = JesusonicAPI.sx_parmIsEnum(instance, paramIndex) != 0;

    // Check for boolean parameter (0-1 range with step=1, not enum)
    if (!isEnum && minVal == 0.0 && maxVal == 1.0 && step == 1.0)
        return ParameterType::Boolean;

    // Check for enum parameter
    if (isEnum)
        return ParameterType::Enum;

    // Check for integer parameter (step >= 1.0)
    if (step >= 1.0)
        return ParameterType::Integer;

    return ParameterType::Float;
}

juce::String ParameterUtils::getParameterName(SX_Instance* instance, int paramIndex)
{
    if (!instance)
        return "Parameter " + juce::String(paramIndex);

    char paramName[256] = {0};
    JesusonicAPI.sx_getParmName(instance, paramIndex, paramName, sizeof(paramName));

    if (paramName[0] != 0)
        return juce::String(paramName);

    return "Parameter " + juce::String(paramIndex);
}

juce::String ParameterUtils::getParameterDisplayText(SX_Instance* instance, int paramIndex, double value)
{
    if (!instance)
        return juce::String(value);

    char displayText[256] = {0};
    JesusonicAPI.sx_getParmDisplay(instance, paramIndex, displayText, sizeof(displayText), &value);

    if (displayText[0] != 0)
        return juce::String(displayText);

    return juce::String(value);
}

bool ParameterUtils::hasParameterChanged(double newValue, double oldValue, double threshold)
{
    return std::abs(newValue - oldValue) > threshold;
}
