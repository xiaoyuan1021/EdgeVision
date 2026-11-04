#include "pipeline_steps.h"
#include "opencv_algorithm.h"
#include "logger.h"
#include <opencv2/line_descriptor.hpp>
#include <cmath>
#include <algorithm>

void StepColorChannel::run(PipelineContext &ctx)
{
    if (ctx.srcBgr.empty() || !ctx.config) return;
    try {
        switch (ctx.config->colorFilter.channel)
        {
        case ChannelMode::Gray:
            cv::cvtColor(ctx.srcBgr, ctx.channelImg, cv::COLOR_BGR2GRAY);
            break;
        case ChannelMode::RGB:
            ctx.channelImg = ctx.srcBgr;
            break;
        case ChannelMode::HSV:
            cv::cvtColor(ctx.srcBgr, ctx.channelImg, cv::COLOR_BGR2HSV);
            break;
        case ChannelMode::B:
            cv::extractChannel(ctx.srcBgr, ctx.channelImg, 0);
            break;
        case ChannelMode::G:
            cv::extractChannel(ctx.srcBgr, ctx.channelImg, 1);
            break;
        case ChannelMode::R:
            cv::extractChannel(ctx.srcBgr, ctx.channelImg, 2);
            break;
        default:
            ctx.channelImg = ctx.srcBgr;
        }
        ctx.visualBase = ctx.channelImg;
    } catch (const cv::Exception& ex) {
spdlog::error("ColorChannel OpenCV错误: {}", ex.what());
        ctx.channelImg = ctx.srcBgr;
        ctx.visualBase = ctx.channelImg;
    } catch (const std::exception& e) {
spdlog::error("ColorChannel 异常: {}", e.what());
        ctx.channelImg = ctx.srcBgr;
        ctx.visualBase = ctx.channelImg;
    } catch (...) {
        spdlog::info("[ColorChannel] 未知异常");
        spdlog::error("ColorChannel 未知异常");
        ctx.channelImg = ctx.srcBgr;
        ctx.visualBase = ctx.channelImg;
    }
}

void StepEnhance::run(PipelineContext& ctx)
{
    {
        if(ctx.srcBgr.empty() || !proc_ || !ctx.config) return;
        if (ctx.channelImg.empty()) return;

        try {
            // EnhanceConfig 使用与 UI 滑块一致的 int 原始值
            // 这里转换为算法所需的比例值（brightness 不需要转换）
            ctx.enhanced=proc_->adjustParameter(
                ctx.channelImg,
                ctx.config->enhance.brightness,
                ctx.config->enhance.contrast / 100.0,
                ctx.config->enhance.gamma / 100.0,
                ctx.config->enhance.sharpen / 100.0
                );
            
            // CLAHE 处理
            if (ctx.config->enhance.claheEnabled) {
                ctx.enhanced = proc_->applyClahe(
                    ctx.enhanced,
                    ctx.config->enhance.claheEnabled,
                    ctx.config->enhance.claheClipLimit / 20.0,
                    ctx.config->enhance.claheGridSize
                );
            }
            
            ctx.visualBase = ctx.enhanced;
        } catch (const cv::Exception& ex) {
spdlog::error("Enhance OpenCV错误: {}", ex.what());
            ctx.enhanced = ctx.channelImg;
            ctx.visualBase = ctx.enhanced;
        } catch (...) {
            spdlog::info("[Enhance] 未知异常");
    spdlog::error("Enhance 未知异常");
            ctx.enhanced = ctx.channelImg;
            ctx.visualBase = ctx.enhanced;
        }
    }

}

void StepFilter::run(PipelineContext& ctx)
{
    if (!ctx.config) return;

    auto mode = ctx.config->colorFilter.mode;

    // 无过滤模式，释放mask并返回
    if (mode == ImageFilterMode::None) {
        ctx.filterMask.release();
        return;
    }

    if (ctx.enhanced.empty()) {
        ctx.filterMask.release();
        return;
    }

    try {
        switch (mode) {
        case ImageFilterMode::Gray: {
            // 灰度过滤
            cv::Mat gray;
            if (ctx.enhanced.channels() == 3) {
                cv::cvtColor(ctx.enhanced, gray, cv::COLOR_BGR2GRAY);
            } else {
                gray = ctx.enhanced;
            }
            if (!gray.isContinuous()) gray = gray.clone();
            cv::inRange(gray,
                        cv::Scalar(ctx.config->colorFilter.grayLow),
                        cv::Scalar(ctx.config->colorFilter.grayHigh),
                        ctx.filterMask);
            ctx.reason = QString("灰度过滤: 范围[%1,%2]")
                             .arg(ctx.config->colorFilter.grayLow)
                             .arg(ctx.config->colorFilter.grayHigh);
            break;
        }
        case ImageFilterMode::RGB: {
            // RGB颜色过滤
            cv::Mat input = ctx.enhanced.empty() ? ctx.srcBgr : ctx.enhanced;
            ctx.filterMask = m_processor->filterRGB(
                input,
                ctx.config->colorFilter.rLow, ctx.config->colorFilter.rHigh,
                ctx.config->colorFilter.gLow, ctx.config->colorFilter.gHigh,
                ctx.config->colorFilter.bLow, ctx.config->colorFilter.bHigh
            );
            ctx.reason = "颜色过滤: RGB模式";
            break;
        }
        case ImageFilterMode::HSV: {
            // HSV颜色过滤
            cv::Mat input = ctx.enhanced.empty() ? ctx.srcBgr : ctx.enhanced;
            ctx.filterMask = m_processor->filterHSV(
                input,
                ctx.config->colorFilter.hLow, ctx.config->colorFilter.hHigh,
                ctx.config->colorFilter.sLow, ctx.config->colorFilter.sHigh,
                ctx.config->colorFilter.vLow, ctx.config->colorFilter.vHigh
            );
            ctx.reason = "颜色过滤: HSV模式";
            break;
        }
        default:
            ctx.filterMask.release();
            break;
        }
    } catch (const cv::Exception& ex) {
spdlog::error("StepFilter OpenCV错误: {}", ex.what());
        ctx.filterMask.release();
        ctx.reason = "过滤失败";
    } catch (const std::exception& e) {
spdlog::error("StepFilter 异常: {}", e.what());
        ctx.filterMask.release();
        ctx.reason = QString("过滤失败: %1").arg(e.what());
    } catch (...) {
        spdlog::info("[StepFilter] 未知异常");
        spdlog::error("StepFilter 未知异常");
        ctx.filterMask.release();
        ctx.reason = "过滤失败: 未知异常";
    }
}

void StepAlgorithmQueue::run(PipelineContext& ctx)
    {
        if (!m_processor || !ctx.config || ctx.config->algorithmQueue.isEmpty())
        {
            return;
        }

        try {
            cv::Mat input;
            // 优先使用过滤结果
            if (!ctx.filterMask.empty())
            {
                input = ctx.filterMask;
            }
            else if (!ctx.enhanced.empty())
            {
                if (ctx.enhanced.channels() == 3) {
                    cv::cvtColor(ctx.enhanced, input, cv::COLOR_BGR2GRAY);
                } else {
                    input = ctx.enhanced;
                }
            }
            else
            {
                if (ctx.srcBgr.channels() == 3) {
                    cv::cvtColor(ctx.srcBgr, input, cv::COLOR_BGR2GRAY);
                } else {
                    input = ctx.srcBgr;
                }
            }

            if (input.empty()) return;

            cv::Mat result = m_processor->executeAlgorithmQueue(input, ctx.config->algorithmQueue);

            if (!result.empty())
            {
                ctx.extractedMask = result;
                ctx.reason = QString("算法队列执行完成 (%1个步骤)")
                                 .arg(ctx.config->algorithmQueue.size());
            }
        } catch (const cv::Exception& ex) {
spdlog::error("AlgorithmQueue OpenCV错误: {}", ex.what());
            ctx.reason = "算法队列执行失败";
        } catch (...) {
            spdlog::info("[AlgorithmQueue] 未知异常");
    spdlog::error("AlgorithmQueue 未知异常");
            ctx.reason = "算法队列执行失败";
        }
    }


void StepShapeFilter::run(PipelineContext& ctx)
    {
        if (!ctx.config) return;
        
        // 使用过滤结果作为输入
        cv::Mat inputMat = ctx.filterMask;

        if (inputMat.empty()) {
            return;
        }

        const ShapeFilterConfig& filter = ctx.config->shapeFilter;

        if (!filter.hasValidConditions()) {
            return;
        }

        try {
            cv::Mat binary;
            if (inputMat.channels() == 3) {
                cv::cvtColor(inputMat, binary, cv::COLOR_BGR2GRAY);
            } else {
                binary = inputMat.clone();
            }
            cv::threshold(binary, binary, 127, 255, cv::THRESH_BINARY);

            cv::Mat labels, stats, centroids;
            int numBefore = cv::connectedComponentsWithStats(binary, labels, stats, centroids, 8) - 1;

            spdlog::info("========== 形状筛选 ==========");
            spdlog::debug("筛选模式: {}", getFilterModeName(filter.mode).toStdString());
            spdlog::debug("筛选前区域数量: {}", numBefore);

            cv::Mat filteredRegion = applyFilter(binary, filter);

            cv::Mat filteredLabels, filteredStats, filteredCentroids;
            int numAfter = cv::connectedComponentsWithStats(filteredRegion, filteredLabels, filteredStats, filteredCentroids, 8) - 1;

            spdlog::debug("筛选后区域数量: {}", numAfter);
            spdlog::info("==============================");

            ctx.regionCount = numAfter;

            if (!filteredRegion.empty()) {
                ctx.extractedMask = filteredRegion;
                ctx.reason = QString("形状筛选: %1, 保留 %2/%3 个区域")
                                 .arg(filter.toString())
                                 .arg(numAfter)
                                 .arg(numBefore);
            }
        }
        catch (const cv::Exception& ex)
        {
spdlog::error("ShapeFilter OpenCV错误: {}", ex.what());
            ctx.reason = "形状筛选失败";
        }
    }

cv::Mat StepShapeFilter::applyFilter(const cv::Mat& regions, const ShapeFilterConfig& config)
    {
        if (config.mode == ShapeFilterLogicMode::And) {
            return applyFilterAnd(regions, config);
        } else {
            return applyFilterOr(regions, config);
        }
    }

cv::Mat StepShapeFilter::applyFilterAnd(const cv::Mat& regions, const ShapeFilterConfig& config)
    {
        cv::Mat result = regions.clone();

        for (const auto& cond : config.conditions)
        {
            if (!cond.isValid()) continue;

            spdlog::info("  应用条件: {}", cond.toString().toStdString());

            result = OpenCVAlgorithm::selectShapeByFeature(
                result,
                QString(getFeatureName(cond.feature)),
                cond.minValue,
                cond.maxValue
            );

            cv::Mat labels, stats, centroids;
            int count = cv::connectedComponentsWithStats(result, labels, stats, centroids, 8) - 1;
            spdlog::info("    剩余区域: {}", count);
        }

        return result;
    }

cv::Mat StepShapeFilter::applyFilterOr(const cv::Mat& regions, const ShapeFilterConfig& config)
    {
        cv::Mat result = cv::Mat::zeros(regions.size(), CV_8UC1);
        bool hasResult = false;

        for (const auto& cond : config.conditions)
        {
            if (!cond.isValid()) continue;

            spdlog::info("  应用条件: {}", cond.toString().toStdString());

            cv::Mat singleResult = OpenCVAlgorithm::selectShapeByFeature(
                regions,
                QString(getFeatureName(cond.feature)),
                cond.minValue,
                cond.maxValue
            );

            cv::Mat labels, stats, centroids;
            int count = cv::connectedComponentsWithStats(singleResult, labels, stats, centroids, 8) - 1;
            spdlog::info("    该条件匹配区域: {}", count);

            if (!hasResult) {
                result = singleResult;
                hasResult = true;
            } else {
                cv::bitwise_or(result, singleResult, result);
            }
        }

        return hasResult ? result : cv::Mat();
    }

// ========== 直线检测辅助函数 ==========

static void detectLinesHoughP(const cv::Mat& edges, const PipelineConfig* cfg, std::vector<cv::Vec4f>& lines)
{
    if (!cfg || edges.empty()) return;
    try {
        std::vector<cv::Vec4i> linesInt;
        cv::HoughLinesP(edges, linesInt, cfg->lineDetect.rho, cfg->lineDetect.theta,
                       cfg->lineDetect.threshold, cfg->lineDetect.minLength, cfg->lineDetect.maxGap);

        for (const auto& line : linesInt) {
            lines.push_back(cv::Vec4f(line[0], line[1], line[2], line[3]));
        }

        spdlog::debug("[HoughP] 检测到 {} 条直线", lines.size());
    } catch (const cv::Exception& ex) {
spdlog::error("HoughP OpenCV错误: {}", ex.what());
    } catch (...) {
        spdlog::info("[HoughP] 未知异常");
    spdlog::error("HoughP 未知异常");
    }
}

static void detectLinesLSD(const cv::Mat& src, std::vector<cv::Vec4f>& lines)
{
    if (src.empty()) return;
    try {
        // 大图预缩放加速：LSD 对全分辨率大图很慢，缩到 800px 宽度以内再检测
        cv::Mat work = src;
        cv::Mat resized;
        constexpr int kMaxDim = 800;
        int maxSide = std::max(src.cols, src.rows);
        if (maxSide > kMaxDim) {
            double scale = (double)kMaxDim / maxSide;
            cv::resize(src, resized, cv::Size(), scale, scale, cv::INTER_AREA);
            work = resized;
        }

        auto LSDDetector = cv::line_descriptor::LSDDetector::createLSDDetector();
        std::vector<cv::line_descriptor::KeyLine> keylines;
        LSDDetector->detect(work, keylines, 2, 1);
        spdlog::debug("[LSD] 检测到 {} 条直线 (输入 {}x{})", keylines.size(), work.cols, work.rows);

        float sx = (work.cols == src.cols) ? 1.0f : (float)src.cols / work.cols;
        float sy = (work.rows == src.rows) ? 1.0f : (float)src.rows / work.rows;
        for (const auto& kl : keylines)
        {
            cv::Point2f p1 = kl.getStartPoint();
            cv::Point2f p2 = kl.getEndPoint();
            if (sx != 1.0f || sy != 1.0f) {
                p1.x *= sx; p1.y *= sy;
                p2.x *= sx; p2.y *= sy;
            }
            if (p1 != p2) {
                lines.push_back(cv::Vec4f(p1.x, p1.y, p2.x, p2.y));
            }
        }
    } catch (const cv::Exception& ex) {
        spdlog::error("LSD OpenCV错误: {}", ex.what());
    } catch (...) {
        spdlog::info("[LSD] 未知异常");
        spdlog::error("LSD 未知异常");
    }
}

// 合并共线线段并用最小二乘拟合
static void mergeAndFitLines(const std::vector<cv::Vec4f>& srcLines,
                              std::vector<cv::Vec4f>& dstLines,
                              double angleThresh = 2.0,    // 角度差阈值（度）
                              double distThresh = 20.0)    // 端点距离阈值（像素）
{
    if (srcLines.empty()) return;
    
    // 1. 计算每条线段的角度（归一化到0~180度）
    struct LineInfo {
        cv::Vec4f line;
        double angle;
        double length;
    };
    std::vector<LineInfo> lineInfos;
    for (const auto& l : srcLines) {
        double dx = l[2] - l[0];
        double dy = l[3] - l[1];
        double angle = std::atan2(dy, dx) * 180.0 / CV_PI;
        if (angle < 0) angle += 180.0;
        double length = std::sqrt(dx * dx + dy * dy);
        lineInfos.push_back({l, angle, length});
    }
    
    // 2. 按角度排序
    std::sort(lineInfos.begin(), lineInfos.end(),
              [](const LineInfo& a, const LineInfo& b) { return a.angle < b.angle; });
    
    // 3. 聚类：角度差 < angleThresh 的归为一类
    std::vector<std::vector<int>> clusters;
    std::vector<bool> assigned(lineInfos.size(), false);
    
    for (size_t i = 0; i < lineInfos.size(); ++i) {
        if (assigned[i]) continue;
        std::vector<int> cluster;
        cluster.push_back(static_cast<int>(i));
        assigned[i] = true;
        
        for (size_t j = i + 1; j < lineInfos.size(); ++j) {
            if (assigned[j]) continue;
            double diff = std::abs(lineInfos[j].angle - lineInfos[i].angle);
            if (diff > 90.0) diff = 180.0 - diff;  // 处理0/180度环绕
            if (diff < angleThresh) {
                // 进一步检查端点距离：至少一个端点在已有线段附近
                bool nearEnough = false;
                for (int idx : cluster) {
                    cv::Point2f p1(lineInfos[idx].line[0], lineInfos[idx].line[1]);
                    cv::Point2f p2(lineInfos[idx].line[2], lineInfos[idx].line[3]);
                    cv::Point2f q1(lineInfos[j].line[0], lineInfos[j].line[1]);
                    cv::Point2f q2(lineInfos[j].line[2], lineInfos[j].line[3]);
                    
                    double d11 = cv::norm(p1 - q1);
                    double d12 = cv::norm(p1 - q2);
                    double d21 = cv::norm(p2 - q1);
                    double d22 = cv::norm(p2 - q2);
                    double minDist = std::min({d11, d12, d21, d22});
                    
                    if (minDist < distThresh) {
                        nearEnough = true;
                        break;
                    }
                }
                if (nearEnough) {
                    cluster.push_back(static_cast<int>(j));
                    assigned[j] = true;
                }
            }
        }
        clusters.push_back(cluster);
    }
    
    // 4. 对每个聚类用 fitLine 拟合
    dstLines.clear();
    for (const auto& cluster : clusters) {
        if (cluster.size() == 1) {
            // 单条线段直接保留
            dstLines.push_back(lineInfos[cluster[0]].line);
            continue;
        }
        
        // 收集聚类中所有端点
        std::vector<cv::Point2f> points;
        for (int idx : cluster) {
            points.push_back(cv::Point2f(lineInfos[idx].line[0], lineInfos[idx].line[1]));
            points.push_back(cv::Point2f(lineInfos[idx].line[2], lineInfos[idx].line[3]));
        }
        
        // 最小二乘拟合
        cv::Vec4f fitVec;
        cv::fitLine(points, fitVec, cv::DIST_L2, 0, 0.01, 0.01);
        
        // fitVec = (vx, vy, x0, y0) 其中(vx,vy)是方向向量，(x0,y0)是线上一点
        cv::Point2f dir(fitVec[0], fitVec[1]);
        cv::Point2f base(fitVec[2], fitVec[3]);
        
        // 将所有端点投影到拟合线上，找最远的两个作为新端点
        float minProj = FLT_MAX, maxProj = -FLT_MAX;
        for (const auto& pt : points) {
            float proj = (pt.x - base.x) * dir.x + (pt.y - base.y) * dir.y;
            minProj = std::min(minProj, proj);
            maxProj = std::max(maxProj, proj);
        }
        
        cv::Point2f newP1(base.x + minProj * dir.x, base.y + minProj * dir.y);
        cv::Point2f newP2(base.x + maxProj * dir.x, base.y + maxProj * dir.y);
        dstLines.push_back(cv::Vec4f(newP1.x, newP1.y, newP2.x, newP2.y));
    }
}

static void detectLinesEDlines(const cv::Mat& src, std::vector<cv::Vec4f>& lines)
{
    if (src.empty()) return;
    try {
        // [BUG WORKAROUND] EDLine (opencv_contrib line_descriptor) 存在已知 bug:
        // EdgeDrawing() 中缓冲区按 pixelNum/5 分配，但写入时不检查边界，
        // 越界后才检测并 return -1，但堆已被破坏 (opencv_contrib#1999)。
        // 预估边像素密度并在超限时缩图，避免超过 EdgeDrawing 的固定容量。
        cv::Mat work = src;
        const int pixelNum = src.cols * src.rows;
        const int edgePixelBudget = pixelNum / 5;  // 库内部缓冲区大小

        // 用轻量 Canny 预估边像素数量（仅采样中间 1/4 区域，避免全图开销）
        int estimatedEdges = 0;
        {
            cv::Mat roi(src, cv::Rect(src.cols / 4, src.rows / 4,
                                      src.cols / 2, src.rows / 2));
            cv::Mat canny;
            cv::Canny(roi, canny, 50, 150);
            estimatedEdges = cv::countNonZero(canny) * 4;  // 推算全图
        }

        if (estimatedEdges > edgePixelBudget) {
            double scale = std::sqrt((double)edgePixelBudget / estimatedEdges) * 0.9;
            scale = std::max(scale, 0.25);  // 不缩得太小
            cv::Mat resized;
            cv::resize(src, resized, cv::Size(), scale, scale, cv::INTER_AREA);
            work = resized;
            spdlog::debug("[EDlines] 边像素 {} 超过预算 {}，缩放至 {}x{}",
                          estimatedEdges, edgePixelBudget, work.cols, work.rows);
        }

        auto edDetector = cv::line_descriptor::BinaryDescriptor::createBinaryDescriptor();
        std::vector<cv::line_descriptor::KeyLine> keylines;
        edDetector->detect(work, keylines);
        spdlog::debug("[EDlines] 检测到 {} 条直线", keylines.size());

        // 如果做了缩放，把坐标映射回原图
        float sx = (work.cols == src.cols) ? 1.0f : (float)src.cols / work.cols;
        float sy = (work.rows == src.rows) ? 1.0f : (float)src.rows / work.rows;
        for (const auto& kl : keylines)
        {
            cv::Point2f p1 = kl.getStartPoint();
            cv::Point2f p2 = kl.getEndPoint();
            if (sx != 1.0f || sy != 1.0f) {
                p1.x *= sx; p1.y *= sy;
                p2.x *= sx; p2.y *= sy;
            }
            if (p1 != p2) {
                lines.push_back(cv::Vec4f(p1.x, p1.y, p2.x, p2.y));
            }
        }
    } catch (const cv::Exception& ex) {
        spdlog::error("EDlines OpenCV错误: {}", ex.what());
    } catch (...) {
        spdlog::info("[EDlines] 未知异常");
        spdlog::error("EDlines 未知异常");
    }
}

void StepLineDetector::run(PipelineContext& ctx)
{
    if (!ctx.config) return;
    if (ctx.srcBgr.empty()) return;

    if (ctx.config->lineDetect.enableReferenceLineMatch) {
        runReferenceLineMatch(ctx);
    } else {
        runLineDetect(ctx);
    }
}

void StepLineDetector::runLineDetect(PipelineContext& ctx)
{
    if (!ctx.config->lineDetect.enabled) return;

    try {
        cv::Mat srcVis = ctx.visualBase.empty() ? ctx.srcBgr : ctx.visualBase;
        cv::Mat srcGray;
        cv::Mat srcColor;

        if (srcVis.channels() == 1) {
            srcGray = srcVis;
            cv::cvtColor(srcVis, srcColor, cv::COLOR_GRAY2BGR);
        } else {
            cv::cvtColor(srcVis, srcGray, cv::COLOR_BGR2GRAY);
            srcColor = srcVis;
        }

        cv::Mat edges;
        cv::Canny(srcGray, edges, 50, 150);

        std::vector<cv::Vec4f> lines;

        if (ctx.config->lineDetect.algorithm == 0) {
            detectLinesHoughP(edges, ctx.config.get(), lines);
        } else if (ctx.config->lineDetect.algorithm == 1) {
            detectLinesLSD(srcGray, lines);
            // LSD输出短线段，合并共线段并拟合
            std::vector<cv::Vec4f> merged;
            mergeAndFitLines(lines, merged, 5.0, 20.0);
            lines = merged;
        } else if (ctx.config->lineDetect.algorithm == 2) {
            detectLinesEDlines(srcGray, lines);
            std::vector<cv::Vec4f> merged;
            mergeAndFitLines(lines, merged, 5.0, 20.0);
            lines = merged;
        }

        ctx.lineDetectImage = srcColor.clone();

        for (const auto& line : lines) {
            cv::line(ctx.lineDetectImage,
                    cv::Point(line[0], line[1]),
                    cv::Point(line[2], line[3]),
                    cv::Scalar(0, 255, 0), 1);
        }

        ctx.totalLineCount = static_cast<int>(lines.size());
    } catch (const cv::Exception& ex) {
        spdlog::error("LineDetector OpenCV错误: {}", ex.what());
        ctx.reason = "直线检测失败";
    } catch (...) {
        spdlog::info("[LineDetector] 未知异常");
        spdlog::error("LineDetector 未知异常");
        ctx.reason = "直线检测失败";
    }
}

// ========== 参考线匹配辅助函数 ==========

static double calculateLineAngle(const cv::Vec4f& line)
{
    cv::Point2f p1(line[0], line[1]);
    cv::Point2f p2(line[2], line[3]);
    double angle = std::atan2(p2.y - p1.y, p2.x - p1.x) * 180.0 / CV_PI;
    return angle;
}

static double pointToLineDistance(const cv::Point2f& point, 
                                  const cv::Point2f& lineStart, 
                                  const cv::Point2f& lineEnd)
{
    cv::Point2f lineVec = lineEnd - lineStart;
    cv::Point2f pointVec = point - lineStart;
    
    double lineLength = cv::norm(lineVec);
    if (lineLength < 1e-6) {
        return cv::norm(pointVec);
    }
    
    double cross = std::abs(lineVec.x * pointVec.y - lineVec.y * pointVec.x);
    return cross / lineLength;
}

static double calculateAngleDifference(double angle1, double angle2)
{
    double diff = std::abs(angle1 - angle2);
    if (diff > 180.0) {
        diff = 360.0 - diff;
    }
    return diff;
}

static bool isLineMatchingReference(const cv::Vec4f& line,
                                    const cv::Point2f& refStart,
                                    const cv::Point2f& refEnd,
                                    double refAngle,
                                    double angleThreshold,
                                    double distanceThreshold)
{
    double lineAngle = calculateLineAngle(line);
    double angleDiff = calculateAngleDifference(lineAngle, refAngle);
    
    if (angleDiff > angleThreshold) {
        return false;
    }
    
    cv::Point2f lineMid((line[0] + line[2]) / 2.0f, (line[1] + line[3]) / 2.0f);
    double dist = pointToLineDistance(lineMid, refStart, refEnd);
    
    if (dist > distanceThreshold) {
        return false;
    }
    
    return true;
}

static double calculateReferenceLineAngle(const PipelineConfig* cfg)
{
    return std::atan2(cfg->lineDetect.referenceLineEnd.y - cfg->lineDetect.referenceLineStart.y,
                      cfg->lineDetect.referenceLineEnd.x - cfg->lineDetect.referenceLineStart.x) * 180.0 / CV_PI;
}

void StepLineDetector::runReferenceLineMatch(PipelineContext& ctx)
{
    if (!ctx.config->lineDetect.enableReferenceLineMatch) {
        return;
    }

    if (!ctx.config->lineDetect.referenceLineValid) {
        ctx.reason = "参考线未绘制";
        return;
    }

    try {
        cv::Mat srcVis = ctx.visualBase.empty() ? ctx.srcBgr : ctx.visualBase;
        cv::Mat src;
        if (srcVis.channels() == 1)
            cv::cvtColor(srcVis, src, cv::COLOR_GRAY2BGR);
        else
            src = srcVis;

        cv::Point2f refStart = ctx.config->lineDetect.referenceLineStart;
        cv::Point2f refEnd = ctx.config->lineDetect.referenceLineEnd;

        cv::Point2f lineDir = refEnd - refStart;
        double lineLength = cv::norm(lineDir);
        if (lineLength < 1e-6) {
            ctx.reason = "参考线长度太短";
            return;
        }

        cv::Point2f normal(-lineDir.y / lineLength, lineDir.x / lineLength);

        double halfWidth = ctx.config->lineDetect.searchRegionWidth / 2.0;
        cv::Point2f offset = normal * halfWidth;

        std::vector<cv::Point> searchRegion = {
            cv::Point(refStart.x + offset.x, refStart.y + offset.y),
            cv::Point(refEnd.x + offset.x, refEnd.y + offset.y),
            cv::Point(refEnd.x - offset.x, refEnd.y - offset.y),
            cv::Point(refStart.x - offset.x, refStart.y - offset.y)
        };

        cv::Mat regionMask = cv::Mat::zeros(src.size(), CV_8UC1);
        std::vector<std::vector<cv::Point>> contours = {searchRegion};
        cv::fillPoly(regionMask, contours, cv::Scalar(255));

        cv::Mat gray;
        cv::cvtColor(src, gray, cv::COLOR_BGR2GRAY);

        cv::Mat maskedGray;
        gray.copyTo(maskedGray, regionMask);

        cv::Mat edges;
        cv::Canny(maskedGray, edges, 50, 150);

        std::vector<cv::Vec4f> allLines;

        if (ctx.config->lineDetect.algorithm == 0) {
            detectLinesHoughP(edges, ctx.config.get(), allLines);
        } else if (ctx.config->lineDetect.algorithm == 1) {
            detectLinesLSD(maskedGray, allLines);
        } else if (ctx.config->lineDetect.algorithm == 2) {
            detectLinesEDlines(maskedGray, allLines);
        }

        double refAngle = calculateReferenceLineAngle(ctx.config.get());

        std::vector<cv::Vec4f> matchedLines;
        for (const auto& line : allLines) {
            if (isLineMatchingReference(line,
                                        ctx.config->lineDetect.referenceLineStart,
                                        ctx.config->lineDetect.referenceLineEnd,
                                        refAngle,
                                        ctx.config->lineDetect.angleThreshold,
                                        ctx.config->lineDetect.distanceThreshold)) {
                matchedLines.push_back(line);
            }
        }

        ctx.lineDetectImage = src.clone();

        cv::line(ctx.lineDetectImage,
                 ctx.config->lineDetect.referenceLineStart,
                 ctx.config->lineDetect.referenceLineEnd,
                 cv::Scalar(0, 255, 255), 4, cv::LINE_AA);

        for (const auto& line : matchedLines) {
            cv::line(ctx.lineDetectImage,
                     cv::Point(line[0], line[1]),
                     cv::Point(line[2], line[3]),
                     cv::Scalar(255, 0, 255), 3, cv::LINE_AA);
        }

        for (const auto& line : allLines) {
            bool isMatched = false;
            for (const auto& matched : matchedLines) {
                if (std::abs(line[0] - matched[0]) < 1 &&
                    std::abs(line[1] - matched[1]) < 1 &&
                    std::abs(line[2] - matched[2]) < 1 &&
                    std::abs(line[3] - matched[3]) < 1) {
                    isMatched = true;
                    break;
                }
            }
            if (!isMatched) {
                cv::line(ctx.lineDetectImage,
                         cv::Point(line[0], line[1]),
                         cv::Point(line[2], line[3]),
                         cv::Scalar(0, 255, 0), 1, cv::LINE_AA);
            }
        }

        ctx.reason = QString("参考线匹配: 找到 %1/%2 条匹配直线 (角度容差:%3°, 距离容差:%4px)")
                         .arg(matchedLines.size())
                         .arg(allLines.size())
                         .arg(ctx.config->lineDetect.angleThreshold)
                         .arg(ctx.config->lineDetect.distanceThreshold);

        ctx.matchedLineCount = static_cast<int>(matchedLines.size());
        ctx.totalLineCount = static_cast<int>(allLines.size());

        spdlog::debug("[LineDetector:ReferenceLineMatch] {}", ctx.reason.toStdString());
    } catch (const cv::Exception& ex) {
spdlog::error("LineDetector:ReferenceLineMatch OpenCV错误: {}", ex.what());
        ctx.reason = "参考线匹配失败";
    } catch (...) {
        spdlog::info("[LineDetector:ReferenceLineMatch] 未知异常");
        spdlog::error("LineDetector:ReferenceLineMatch 未知异常");
        ctx.reason = "参考线匹配失败";
    }
}