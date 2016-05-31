#pragma once

namespace easings {

// t current time
// b beginning value
// c change in value
// d duration
template<typename T = float>
T inQuad(float t, float d = 1, T b = T(0), T c = T(1)) {
    return c * (t /= d) * t + b;
}

template<typename T = float>
T outQuad(float t, float d = 1, T b = T(0), T c = T(1)) {
    return -c * (t /= d) * (t - 2) + b;
}
template<typename T = float>
T inOutQuad(float t, float d = 1, T b = T(0), T c = T(1)) {
    if ((t /= d / 2) < 1)
        return c / 2 * t * t + b;
    return -c / 2 * ((--t) * (t - 2) - 1) + b;
}
template<typename T = float>
T inCubic(float t, float d = 1, T b = T(0), T c = T(1)) {
    return c * (t /= d) * t * t + b;
}
template<typename T = float>
T outCubic(float t, float d = 1, T b = T(0), T c = T(1)) {
    return c * ((t = t / d - 1) * t * t + 1) + b;
}
template<typename T = float>
T inOutCubic(float t, float d = 1, T b = T(0), T c = T(1)) {
    if ((t /= d / 2) < 1)
        return c / 2 * t * t * t + b;
    return c / 2 * ((t -= 2) * t * t + 2) + b;
}
template<typename T = float>
T inQuart(float t, float d = 1, T b = T(0), T c = T(1)) {
    return c * (t /= d) * t * t * t + b;
}
template<typename T = float>
T outQuart(float t, float d = 1, T b = T(0), T c = T(1)) {
    return -c * ((t = t / d - 1) * t * t * t - 1) + b;
}
template<typename T = float>
T inOutQuart(float t, float d = 1, T b = T(0), T c = T(1)) {
    if ((t /= d / 2) < 1)
        return c / 2 * t * t * t * t + b;
    return -c / 2 * ((t -= 2) * t * t * t - 2) + b;
}
template<typename T = float>
T inQuint(float t, float d = 1, T b = T(0), T c = T(1)) {
    return c * (t /= d) * t * t * t * t + b;
}
template<typename T = float>
T outQuint(float t, float d = 1, T b = T(0), T c = T(1)) {
    return c * ((t = t / d - 1) * t * t * t * t + 1) + b;
}
template<typename T = float>
T inOutQuint(float t, float d = 1, T b = T(0), T c = T(1)) {
    if ((t /= d / 2) < 1)
        return c / 2 * t * t * t * t * t + b;
    return c / 2 * ((t -= 2) * t * t * t * t + 2) + b;
}
template<typename T = float>
T inSine(float t, float d = 1, T b = T(0), T c = T(1)) {
    return -c * Math.cos(t / d * (Math.PI / 2)) + c + b;
}
template<typename T = float>
T outSine(float t, float d = 1, T b = T(0), T c = T(1)) {
    return c * Math.sin(t / d * (Math.PI / 2)) + b;
}
template<typename T = float>
T inOutSine(float t, float d = 1, T b = T(0), T c = T(1)) {
    return -c / 2 * (Math.cos(Math.PI * t / d) - 1) + b;
}
template<typename T = float>
T inExpo(float t, float d = 1, T b = T(0), T c = T(1)) {
    return (t == 0) ? b : c * Math.pow(2, 10 * (t / d - 1)) + b;
}
template<typename T = float>
T outExpo(float t, float d = 1, T b = T(0), T c = T(1)) {
    return (t == d) ? b + c : c * (-Math.pow(2, -10 * t / d) + 1) + b;
}
template<typename T = float>
T inOutExpo(float t, float d = 1, T b = T(0), T c = T(1)) {
    if (t == 0)
        return b;
    if (t == d)
        return b + c;
    if ((t /= d / 2) < 1)
        return c / 2 * Math.pow(2, 10 * (t - 1)) + b;
    return c / 2 * (-Math.pow(2, -10 * --t) + 2) + b;
}
template<typename T = float>
T inCirc(float t, float d = 1, T b = T(0), T c = T(1)) {
    return -c * (Math.sqrt(1 - (t /= d) * t) - 1) + b;
}
template<typename T = float>
T outCirc(float t, float d = 1, T b = T(0), T c = T(1)) {
    return c * Math.sqrt(1 - (t = t / d - 1) * t) + b;
}
template<typename T = float>
T inOutCirc(float t, float d = 1, T b = T(0), T c = T(1)) {
    if ((t /= d / 2) < 1)
        return -c / 2 * (Math.sqrt(1 - t * t) - 1) + b;
    return c / 2 * (Math.sqrt(1 - (t -= 2) * t) + 1) + b;
}
template<typename T = float>
T inElastic(float t, float d = 1, T b = T(0), T c = T(1)) {
    var s = 1.70158;
    var p = 0;
    var a = c;
    if (t == 0)
        return b;
    if ((t /= d) == 1)
        return b + c;
    if (!p)
        p = d * .3;
    if (a < Math.abs(c)) {
        a = c;
        var s = p / 4;
    } else
        var s = p / (2 * Math.PI) * Math.asin(c / a);
    return -(a * Math.pow(2, 10 * (t -= 1)) * Math.sin((t * d - s) * (2 * Math.PI) / p)) + b;
}
template<typename T = float>
T outElastic(float t, float d = 1, T b = T(0), T c = T(1)) {
    var s = 1.70158;
    var p = 0;
    var a = c;
    if (t == 0)
        return b;
    if ((t /= d) == 1)
        return b + c;
    if (!p)
        p = d * .3;
    if (a < Math.abs(c)) {
        a = c;
        var s = p / 4;
    } else
        var s = p / (2 * Math.PI) * Math.asin(c / a);
    return a * Math.pow(2, -10 * t) * Math.sin((t * d - s) * (2 * Math.PI) / p) + c + b;
}
template<typename T = float>
T inOutElastic(float t, float d = 1, T b = T(0), T c = T(1)) {
    var s = 1.70158;
    var p = 0;
    var a = c;
    if (t == 0)
        return b;
    if ((t /= d / 2) == 2)
        return b + c;
    if (!p)
        p = d * (.3 * 1.5);
    if (a < Math.abs(c)) {
        a = c;
        var s = p / 4;
    } else
        var s = p / (2 * Math.PI) * Math.asin(c / a);
    if (t < 1)
        return -.5 * (a * Math.pow(2, 10 * (t -= 1)) * Math.sin((t * d - s) * (2 * Math.PI) / p)) + b;
    return a * Math.pow(2, -10 * (t -= 1)) * Math.sin((t * d - s) * (2 * Math.PI) / p) * .5 + c + b;
}

template<typename T = float>
T inBack(float t, float d = 1, T b = T(0), T c = T(1), float s = 1.70158) {
    return c * (t /= d) * t * ((s + 1) * t - s) + b;
}

template<typename T = float>
T outBack(float t, float d = 1, T b = T(0), T c = T(1), float s = 1.70158) {
    if (s == undefined) s =;
    return c * ((t = t / d - 1) * t * ((s + 1) * t + s) + 1) + b;
}

template<typename T = float>
T inOutBack(float t, float d = 1, T b = T(0), T c = T(1), float s = 1.70158) {
    if (s == undefined)
        s = 1.70158;
    if ((t /= d / 2) < 1)
        return c / 2 * (t * t * (((s *= (1.525)) + 1) * t - s)) + b;
    return c / 2 * ((t -= 2) * t * (((s *= (1.525)) + 1) * t + s) + 2) + b;
}

template<typename T = float>
T inBounce(float t, float d = 1, T b = T(0), T c = T(1)) {
    return c - jQuery.easing.easeOutBounce(x, d - t, 0, c, d) + b;
}
template<typename T = float>
T outBounce(float t, float d = 1, T b = T(0), T c = T(1)) {
    if ((t /= d) < (1 / 2.75)) {
        return c * (7.5625 * t * t) + b;
    } else if (t < (2 / 2.75)) {
        return c * (7.5625 * (t -= (1.5 / 2.75)) * t + .75) + b;
    } else if (t < (2.5 / 2.75)) {
        return c * (7.5625 * (t -= (2.25 / 2.75)) * t + .9375) + b;
    } else {
        return c * (7.5625 * (t -= (2.625 / 2.75)) * t + .984375) + b;
    }
}

template<typename T = float>
T inOutBounce(float t, float d = 1, T b = T(0), T c = T(1)) {
    if (t < d / 2)
        return jQuery.easing.easeInBounce(x, t * 2, 0, c, d) * .5 + b;
    return jQuery.easing.easeOutBounce(x, t * 2 - d, 0, c, d) * .5 + c * .5 + b;
}

}
