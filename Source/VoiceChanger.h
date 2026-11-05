#pragma once
#include <vector>
#include <cmath>
#include <cstdint>
#include <algorithm>

// Мужской -> женский голос: сдвиг высоты (pitch) и независимый сдвиг формант.
// Метод: питч-синхронный гранулярный синтез (PSOLA-подобный).
//  - YIN-детектор находит период основного тона T0
//  - зерна (2 периода, окно Ханна) вырезаются вокруг пика периода (glottal pulse)
//  - зёрна расставляются с новым периодом T0/ratio  -> меняется высота
//  - чтение зерна с шагом F (formant ratio)          -> сдвигаются форманты
// Без FFT, без "роботизации" фазового вокодера, форманты контролируются отдельно.
class VoiceChanger
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        latency = (int) std::lround (0.040 * sr);
        size = 1;
        while (size < (int) (sr * 0.5)) size <<= 1;
        mask = size - 1;
        dec = std::max (1, (int) std::lround (sr / 11025.0));
        const double srD = sr / dec;
        minLag = std::max (2, (int) (srD / 450.0));
        maxLag = (int) (srD / 65.0);
        total  = N + maxLag + 4;
        xd.assign ((size_t) total, 0.f);
        df.assign ((size_t) maxLag + 2, 0.f);
        cm.assign ((size_t) maxLag + 2, 0.f);
        reset();
    }

    void reset()
    {
        inBuf.assign ((size_t) size, 0.f);
        outBuf.assign ((size_t) size, 0.f);
        n = 0;
        nextStart = 0.0;
        t0 = sr / 150.0;
        voiced = false;
        octaveHold = 0;
        detStart = 0;
    }

    int   getLatency() const { return latency; }
    float getInputHz() const { return voiced ? (float) (sr / t0) : 0.f; }

    // ratio = множитель высоты (2^(st/12)), formant = множитель формант (1.0 = без изменений)
    float process (float x, float ratio, float formant)
    {
        inBuf[(size_t) (n & mask)] = x;
        while (n >= (int64_t) std::floor (nextStart))
            makeGrain (ratio, formant);
        const size_t i = (size_t) (n & mask);
        const float y = outBuf[i];
        outBuf[i] = 0.f;
        ++n;
        return y;
    }

private:
    static constexpr int N = 256;   // окно интегрирования YIN (в прореженных сэмплах)

    double sr = 44100.0;
    int latency = 1764, size = 0, mask = 0, dec = 4, minLag = 24, maxLag = 169, total = 429;
    std::vector<float> inBuf, outBuf, xd, df, cm;
    int64_t n = 0, detStart = 0;
    double nextStart = 0.0, t0 = 300.0;
    bool voiced = false;
    int octaveHold = 0;

    float get (int64_t i) const
    {
        if (i < 0 || i > n || i < n - size + 8) return 0.f;
        return inBuf[(size_t) (i & mask)];
    }

    float read (double pos) const
    {
        const int64_t i = (int64_t) std::floor (pos);
        const float f = (float) (pos - (double) i);
        return get (i) * (1.f - f) + get (i + 1) * f;
    }

    void analyse (int64_t ci)
    {
        detStart = ci - (int64_t) (total / 2) * dec;
        double energy = 0.0;
        for (int k = 0; k < total; ++k)
        {
            float s = 0.f;
            for (int j = 0; j < dec; ++j) s += get (detStart + (int64_t) k * dec + j);
            s /= (float) dec;
            xd[(size_t) k] = s;
            energy += (double) s * s;
        }
        energy /= total;
        if (energy < 1e-7) { voiced = false; return; }   // тишина

        const int half0 = maxLag / 2;
        for (int tau = 1; tau <= maxLag; ++tau)
        {
            const int a = half0 - tau / 2, b = a + tau;
            float d = 0.f;
            for (int j = 0; j < N; ++j)
            {
                const float diff = xd[(size_t) (a + j)] - xd[(size_t) (b + j)];
                d += diff * diff;
            }
            df[(size_t) tau] = d;
        }

        cm[0] = 1.f;
        double run = 0.0;
        for (int tau = 1; tau <= maxLag; ++tau)
        {
            run += df[(size_t) tau];
            cm[(size_t) tau] = run > 1e-12 ? (float) (df[(size_t) tau] * tau / run) : 1.f;
        }

        int best = -1;
        float bestV = 1e9f;
        for (int tau = minLag; tau <= maxLag; ++tau)
        {
            if (cm[(size_t) tau] < 0.15f)
            {
                while (tau + 1 <= maxLag && cm[(size_t) tau + 1] < cm[(size_t) tau]) ++tau;
                best = tau; bestV = cm[(size_t) tau];
                break;
            }
            if (cm[(size_t) tau] < bestV) { bestV = cm[(size_t) tau]; best = tau; }
        }

        if (best < 0 || bestV > 0.30f) { voiced = false; return; }

        float shift = 0.f;
        if (best > 1 && best < maxLag)
        {
            const float a = cm[(size_t) best - 1], b = cm[(size_t) best], c = cm[(size_t) best + 1];
            const float den = a - 2.f * b + c;
            if (std::abs (den) > 1e-9f) shift = std::clamp (0.5f * (a - c) / den, -1.f, 1.f);
        }
        const double newT0 = (best + shift) * dec;
        if (voiced && std::abs (newT0 - t0) < 0.25 * t0)
        {
            t0 = 0.5 * t0 + 0.5 * newT0;
            octaveHold = 0;
        }
        else if (voiced && octaveHold < 3
                 && (std::abs (newT0 - 2.0 * t0) < 0.12 * 2.0 * t0 || std::abs (newT0 - 0.5 * t0) < 0.12 * 0.5 * t0))
        {
            ++octaveHold;          // скачок на октаву = почти всегда ошибка детектора: держим прежний период
        }
        else
        {
            t0 = newT0;
            octaveHold = 0;
        }
        voiced = true;
    }

    int64_t snapToPeak (int64_t ci) const
    {
        const int half = (int) (t0 * 0.5);
        int64_t bestS = ci;
        float bestV = -1e9f;
        for (int64_t s = ci - half; s <= ci + half; s += dec)
        {
            const int64_t idx = (s - detStart) / dec;
            if (idx < 0 || idx >= total) continue;
            const float v = xd[(size_t) idx];
            if (v > bestV) { bestV = v; bestS = s; }
        }
        bestS += dec / 2;
        int64_t br = bestS;
        float bv = -1e9f;
        for (int64_t s = bestS - dec; s <= bestS + dec; ++s)
        {
            const float v = get (s);
            if (v > bv) { bv = v; br = s; }
        }
        return br;
    }

    void makeGrain (float ratio, float formant)
    {
        double tsyn = voiced ? t0 / (double) ratio : 0.006 * sr;
        tsyn = std::clamp (tsyn, 0.0015 * sr, 0.030 * sr);

        const double tc = nextStart + tsyn;
        int64_t ci = (int64_t) std::floor (tc) - latency;
        analyse (ci);
        if (voiced) ci = snapToPeak (ci);

        const int L = (int) std::ceil (2.0 * tsyn);
        const int64_t i0 = (int64_t) std::floor (nextStart);
        const float fr = (float) (nextStart - (double) i0);
        const float twoPi = 6.28318530718f;

        for (int k = 0; k < L; ++k)
        {
            const float w = 0.5f - 0.5f * std::cos (twoPi * ((float) k + 0.5f) / (float) L);
            const double src = (double) ci + ((double) k + 0.5 - 0.5 * L) * (double) formant;
            const float v = read (src) * w;
            const size_t a = (size_t) ((i0 + k) & mask);
            const size_t b = (size_t) ((i0 + k + 1) & mask);
            outBuf[a] += v * (1.f - fr);
            outBuf[b] += v * fr;
        }
        nextStart += tsyn;
    }
};
