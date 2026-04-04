#pragma once

#include <algorithm>
#include <memory>
#include <nk/layout/box_layout.h>
#include <nk/platform/events.h>
#include <nk/platform/key_codes.h>
#include <nk/render/snapshot_context.h>
#include <nk/text/font.h>
#include <nk/ui_core/widget.h>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace mapperbus::frontend {

class Box : public nk::Widget {
  public:
    static std::shared_ptr<Box> vertical(float spacing = 8.0F) {
        auto box = std::shared_ptr<Box>(new Box());
        auto layout = std::make_unique<nk::BoxLayout>(nk::Orientation::Vertical);
        layout->set_spacing(spacing);
        box->set_layout_manager(std::move(layout));
        return box;
    }

    static std::shared_ptr<Box> horizontal(float spacing = 8.0F) {
        auto box = std::shared_ptr<Box>(new Box());
        auto layout = std::make_unique<nk::BoxLayout>(nk::Orientation::Horizontal);
        layout->set_spacing(spacing);
        box->set_layout_manager(std::move(layout));
        return box;
    }

    void append(std::shared_ptr<nk::Widget> child) {
        append_child(std::move(child));
    }

  private:
    Box() = default;
};

class Spacer : public nk::Widget {
  public:
    static std::shared_ptr<Spacer> create() {
        auto spacer = std::shared_ptr<Spacer>(new Spacer());
        spacer->set_horizontal_size_policy(nk::SizePolicy::Expanding);
        spacer->set_horizontal_stretch(1);
        return spacer;
    }

    [[nodiscard]] nk::SizeRequest measure(const nk::Constraints& /*constraints*/) const override {
        return {0.0F, 0.0F, 0.0F, 0.0F};
    }

  protected:
    void snapshot(nk::SnapshotContext& /*ctx*/) const override {}

  private:
    Spacer() = default;
};

class ContentSlot : public nk::Widget {
  public:
    static std::shared_ptr<ContentSlot> create() {
        auto slot = std::shared_ptr<ContentSlot>(new ContentSlot());
        slot->set_horizontal_size_policy(nk::SizePolicy::Expanding);
        return slot;
    }

    void set_child(std::shared_ptr<nk::Widget> child) {
        preserve_damage_regions_for_next_redraw();
        if (child_) {
            remove_child(*child_);
        }
        child_ = std::move(child);
        if (child_) {
            append_child(child_);
        }
        queue_layout();
        queue_redraw();
    }

    [[nodiscard]] nk::SizeRequest measure(const nk::Constraints& constraints) const override {
        if (!child_) {
            return {0.0F, 0.0F, 0.0F, 0.0F};
        }
        return child_->measure(constraints);
    }

    void allocate(const nk::Rect& allocation) override {
        Widget::allocate(allocation);
        if (child_) {
            child_->allocate(allocation);
        }
    }

  private:
    ContentSlot() = default;

    std::shared_ptr<nk::Widget> child_;
};

class FixedWidthSlot : public nk::Widget {
  public:
    static std::shared_ptr<FixedWidthSlot> create(float width,
                                                  std::shared_ptr<nk::Widget> child = nullptr) {
        auto slot = std::shared_ptr<FixedWidthSlot>(new FixedWidthSlot(width));
        if (child) {
            slot->set_child(std::move(child));
        }
        return slot;
    }

    void set_child(std::shared_ptr<nk::Widget> child) {
        if (child_) {
            remove_child(*child_);
        }
        child_ = std::move(child);
        if (child_) {
            append_child(child_);
        }
        queue_layout();
    }

    [[nodiscard]] nk::SizeRequest measure(const nk::Constraints& constraints) const override {
        if (!child_) {
            return {width_, 0.0F, width_, 0.0F};
        }

        auto child_constraints = constraints;
        child_constraints.min_width = width_;
        child_constraints.max_width = width_;
        const auto req = child_->measure(child_constraints);
        return {width_, req.minimum_height, width_, req.natural_height};
    }

    void allocate(const nk::Rect& allocation) override {
        Widget::allocate(allocation);
        if (child_) {
            child_->allocate(allocation);
        }
    }

  private:
    explicit FixedWidthSlot(float width) : width_(std::max(0.0F, width)) {
        set_horizontal_size_policy(nk::SizePolicy::Fixed);
    }

    float width_ = 0.0F;
    std::shared_ptr<nk::Widget> child_;
};

class SectionTitle : public nk::Widget {
  public:
    static std::shared_ptr<SectionTitle> create(std::string text) {
        return std::shared_ptr<SectionTitle>(new SectionTitle(std::move(text)));
    }

    void set_text(std::string text) {
        if (text_ != text) {
            text_ = std::move(text);
            cached_size_.reset();
            queue_layout();
            queue_redraw();
        }
    }

    [[nodiscard]] nk::SizeRequest measure(const nk::Constraints& /*constraints*/) const override {
        if (!cached_size_) {
            cached_size_ = measure_text(text_, font_descriptor());
        }
        const float height = std::max(28.0F, cached_size_->height);
        return {cached_size_->width, height, cached_size_->width, height};
    }

  protected:
    void snapshot(nk::SnapshotContext& ctx) const override {
        const auto a = allocation();
        if (!cached_size_) {
            cached_size_ = measure_text(text_, font_descriptor());
        }
        const float text_y = a.y + std::max(0.0F, (a.height - cached_size_->height) * 0.5F);
        ctx.add_text({a.x, text_y},
                     text_,
                     theme_color("text-color", nk::Color{0.14F, 0.16F, 0.19F, 1.0F}),
                     font_descriptor());
    }

  private:
    explicit SectionTitle(std::string text) : text_(std::move(text)) {}

    static nk::FontDescriptor font_descriptor() {
        return {
            .family = {},
            .size = 18.0F,
            .weight = nk::FontWeight::Medium,
        };
    }

    std::string text_;
    mutable std::optional<nk::Size> cached_size_;
};

class SecondaryText : public nk::Widget {
  public:
    static std::shared_ptr<SecondaryText> create(std::string text) {
        return std::shared_ptr<SecondaryText>(new SecondaryText(std::move(text)));
    }

    void set_text(std::string text) {
        if (text_ != text) {
            text_ = std::move(text);
            cached_size_.reset();
            queue_layout();
            queue_redraw();
        }
    }

    [[nodiscard]] nk::SizeRequest measure(const nk::Constraints& /*constraints*/) const override {
        if (!cached_size_) {
            cached_size_ = measure_text(text_, font_descriptor());
        }
        const float height = std::max(20.0F, cached_size_->height);
        return {cached_size_->width, height, cached_size_->width, height};
    }

  protected:
    void snapshot(nk::SnapshotContext& ctx) const override {
        const auto a = allocation();
        if (!cached_size_) {
            cached_size_ = measure_text(text_, font_descriptor());
        }
        const float text_y = a.y + std::max(0.0F, (a.height - cached_size_->height) * 0.5F);
        ctx.add_text({a.x, text_y},
                     text_,
                     theme_color("text-color", nk::Color{0.40F, 0.44F, 0.49F, 1.0F}),
                     font_descriptor());
    }

  private:
    explicit SecondaryText(std::string text) : text_(std::move(text)) {}

    static nk::FontDescriptor font_descriptor() {
        return {
            .family = {},
            .size = 13.0F,
            .weight = nk::FontWeight::Regular,
        };
    }

    std::string text_;
    mutable std::optional<nk::Size> cached_size_;
};

class FieldLabel : public nk::Widget {
  public:
    static std::shared_ptr<FieldLabel> create(std::string text) {
        return std::shared_ptr<FieldLabel>(new FieldLabel(std::move(text)));
    }

    [[nodiscard]] nk::SizeRequest measure(const nk::Constraints& /*constraints*/) const override {
        if (!cached_size_) {
            cached_size_ = measure_text(text_, font_descriptor());
        }
        const float height = std::max(18.0F, cached_size_->height);
        return {cached_size_->width, height, cached_size_->width, height};
    }

  protected:
    void snapshot(nk::SnapshotContext& ctx) const override {
        const auto a = allocation();
        if (!cached_size_) {
            cached_size_ = measure_text(text_, font_descriptor());
        }
        const float text_y = a.y + std::max(0.0F, (a.height - cached_size_->height) * 0.5F);
        ctx.add_text({a.x, text_y},
                     text_,
                     theme_color("text-color", nk::Color{0.52F, 0.56F, 0.61F, 1.0F}),
                     font_descriptor());
    }

  private:
    explicit FieldLabel(std::string text) : text_(std::move(text)) {}

    static nk::FontDescriptor font_descriptor() {
        return {
            .family = {},
            .size = 12.0F,
            .weight = nk::FontWeight::Medium,
        };
    }

    std::string text_;
    mutable std::optional<nk::Size> cached_size_;
};

class ValueText : public nk::Widget {
  public:
    static std::shared_ptr<ValueText> create(std::string text) {
        return std::shared_ptr<ValueText>(new ValueText(std::move(text)));
    }

    void set_text(std::string text) {
        if (text_ != text) {
            text_ = std::move(text);
            cached_size_.reset();
            queue_layout();
            queue_redraw();
        }
    }

    [[nodiscard]] nk::SizeRequest measure(const nk::Constraints& /*constraints*/) const override {
        if (!cached_size_) {
            cached_size_ = measure_text(text_, font_descriptor());
        }
        const float height = std::max(24.0F, cached_size_->height);
        return {cached_size_->width, height, cached_size_->width, height};
    }

  protected:
    void snapshot(nk::SnapshotContext& ctx) const override {
        const auto a = allocation();
        if (!cached_size_) {
            cached_size_ = measure_text(text_, font_descriptor());
        }
        const float text_y = a.y + std::max(0.0F, (a.height - cached_size_->height) * 0.5F);
        ctx.add_text({a.x, text_y},
                     text_,
                     theme_color("text-color", nk::Color{0.16F, 0.18F, 0.22F, 1.0F}),
                     font_descriptor());
    }

  private:
    explicit ValueText(std::string text) : text_(std::move(text)) {}

    static nk::FontDescriptor font_descriptor() {
        return {
            .family = {},
            .size = 17.0F,
            .weight = nk::FontWeight::Medium,
        };
    }

    std::string text_;
    mutable std::optional<nk::Size> cached_size_;
};

class SurfacePanel : public nk::Widget {
  public:
    static std::shared_ptr<SurfacePanel> card(std::shared_ptr<nk::Widget> content) {
        auto panel = std::shared_ptr<SurfacePanel>(new SurfacePanel());
        panel->set_horizontal_size_policy(nk::SizePolicy::Expanding);
        panel->set_content(std::move(content));
        return panel;
    }

    void set_content(std::shared_ptr<nk::Widget> content) {
        if (content_) {
            remove_child(*content_);
        }
        content_ = std::move(content);
        if (content_) {
            append_child(content_);
        }
        queue_layout();
    }

    [[nodiscard]] nk::SizeRequest measure(const nk::Constraints& constraints) const override {
        const float padding = 16.0F;
        if (!content_) {
            return {padding * 2.0F, padding * 2.0F, padding * 2.0F, padding * 2.0F};
        }

        const auto req = content_->measure(constraints);
        return {
            req.minimum_width + (padding * 2.0F),
            req.minimum_height + (padding * 2.0F),
            req.natural_width + (padding * 2.0F),
            req.natural_height + (padding * 2.0F),
        };
    }

    void allocate(const nk::Rect& allocation) override {
        Widget::allocate(allocation);
        if (!content_) {
            return;
        }

        const float padding = 16.0F;
        content_->allocate({
            allocation.x + padding + 1.0F,
            allocation.y + padding + 1.0F,
            std::max(0.0F, allocation.width - ((padding + 1.0F) * 2.0F)),
            std::max(0.0F, allocation.height - ((padding + 1.0F) * 2.0F)),
        });
    }

  protected:
    void snapshot(nk::SnapshotContext& ctx) const override {
        const auto a = allocation();
        constexpr float corner_radius = 18.0F;

        ctx.add_rounded_rect(
            {a.x, a.y + 1.0F, a.width, a.height}, nk::Color{0.08F, 0.12F, 0.18F, 0.04F}, 19.0F);
        ctx.add_rounded_rect(a, nk::Color{0.985F, 0.989F, 0.996F, 1.0F}, corner_radius);
        ctx.add_border(a, nk::Color{0.86F, 0.88F, 0.91F, 1.0F}, 1.0F, corner_radius);

        Widget::snapshot(ctx);
    }

  private:
    SurfacePanel() = default;

    std::shared_ptr<nk::Widget> content_;
};

class StatusPill : public nk::Widget {
  public:
    static std::shared_ptr<StatusPill> create(std::string text, bool emphasized = false) {
        return std::shared_ptr<StatusPill>(new StatusPill(std::move(text), emphasized));
    }

    void set_text(std::string text) {
        if (text_ != text) {
            text_ = std::move(text);
            cached_size_.reset();
            queue_layout();
            queue_redraw();
        }
    }

    [[nodiscard]] nk::SizeRequest measure(const nk::Constraints& /*constraints*/) const override {
        if (!cached_size_) {
            cached_size_ = measure_text(text_, font_descriptor());
        }
        const float width = cached_size_->width + 28.0F;
        return {width, 34.0F, width, 34.0F};
    }

  protected:
    void snapshot(nk::SnapshotContext& ctx) const override {
        const auto a = allocation();
        const auto background = emphasized_ ? nk::Color{0.15F, 0.48F, 0.47F, 1.0F}
                                            : nk::Color{0.94F, 0.96F, 0.98F, 1.0F};
        const auto border = emphasized_ ? nk::Color{0.15F, 0.48F, 0.47F, 1.0F}
                                        : nk::Color{0.84F, 0.87F, 0.91F, 1.0F};
        const auto text_color =
            emphasized_ ? nk::Color{1.0F, 1.0F, 1.0F, 1.0F} : nk::Color{0.28F, 0.31F, 0.36F, 1.0F};

        ctx.add_rounded_rect(a, background, a.height * 0.5F);
        ctx.add_border(a, border, 1.0F, a.height * 0.5F);

        if (!cached_size_) {
            cached_size_ = measure_text(text_, font_descriptor());
        }
        const float text_x = a.x + std::max(0.0F, (a.width - cached_size_->width) * 0.5F);
        const float text_y = a.y + std::max(0.0F, (a.height - cached_size_->height) * 0.5F);
        ctx.add_text({text_x, text_y}, text_, text_color, font_descriptor());
    }

  private:
    StatusPill(std::string text, bool emphasized)
        : text_(std::move(text)), emphasized_(emphasized) {}

    static nk::FontDescriptor font_descriptor() {
        return {
            .family = {},
            .size = 13.0F,
            .weight = nk::FontWeight::Medium,
        };
    }

    std::string text_;
    bool emphasized_ = false;
    mutable std::optional<nk::Size> cached_size_;
};

class InsetStage : public nk::Widget {
  public:
    static std::shared_ptr<InsetStage> create(std::shared_ptr<nk::Widget> content,
                                              float min_height,
                                              float natural_height,
                                              float padding = 10.0F) {
        auto stage =
            std::shared_ptr<InsetStage>(new InsetStage(min_height, natural_height, padding));
        stage->set_horizontal_size_policy(nk::SizePolicy::Expanding);
        stage->set_content(std::move(content));
        return stage;
    }

    void set_content(std::shared_ptr<nk::Widget> content) {
        if (content_) {
            remove_child(*content_);
        }
        content_ = std::move(content);
        if (content_) {
            append_child(content_);
        }
        queue_layout();
    }

    [[nodiscard]] nk::SizeRequest measure(const nk::Constraints& constraints) const override {
        if (!content_) {
            return {0.0F, min_height_, 0.0F, natural_height_};
        }

        const auto req = content_->measure(constraints);
        return {
            req.minimum_width + (padding_ * 2.0F),
            std::max(min_height_, req.minimum_height + (padding_ * 2.0F)),
            req.natural_width + (padding_ * 2.0F),
            std::max(natural_height_, req.natural_height + (padding_ * 2.0F)),
        };
    }

    void allocate(const nk::Rect& allocation) override {
        Widget::allocate(allocation);
        if (!content_) {
            return;
        }

        content_->allocate({
            allocation.x + padding_,
            allocation.y + padding_,
            std::max(0.0F, allocation.width - (padding_ * 2.0F)),
            std::max(0.0F, allocation.height - (padding_ * 2.0F)),
        });
    }

  protected:
    void snapshot(nk::SnapshotContext& ctx) const override {
        const auto a = allocation();
        ctx.add_color_rect(a, nk::Color{0.972F, 0.978F, 0.988F, 1.0F});
        ctx.add_border(a, nk::Color{0.88F, 0.90F, 0.93F, 1.0F}, 1.0F, 0.0F);

        ctx.push_container(a);
        Widget::snapshot(ctx);
        ctx.pop_container();
    }

  private:
    InsetStage(float min_height, float natural_height, float padding)
        : min_height_(min_height), natural_height_(natural_height), padding_(padding) {}

    std::shared_ptr<nk::Widget> content_;
    float min_height_ = 0.0F;
    float natural_height_ = 0.0F;
    float padding_ = 0.0F;
};

class PreviewCanvas : public nk::Widget {
  public:
    static std::shared_ptr<PreviewCanvas> create() {
        auto canvas = std::shared_ptr<PreviewCanvas>(new PreviewCanvas());
        canvas->set_horizontal_size_policy(nk::SizePolicy::Expanding);
        canvas->set_vertical_size_policy(nk::SizePolicy::Expanding);
        canvas->set_vertical_stretch(1);
        return canvas;
    }

    void update_pixel_buffer(const uint32_t* data, int width, int height) {
        const auto count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
        pixels_.assign(data, data + count);
        src_width_ = width;
        src_height_ = height;
        queue_redraw();
    }

    void set_scale_mode(nk::ScaleMode mode) {
        if (scale_mode_ != mode) {
            scale_mode_ = mode;
            queue_redraw();
        }
    }

    [[nodiscard]] nk::ScaleMode scale_mode() const {
        return scale_mode_;
    }

    [[nodiscard]] nk::SizeRequest measure(const nk::Constraints& /*constraints*/) const override {
        return {420.0F, 420.0F, 760.0F, 620.0F};
    }

  protected:
    void snapshot(nk::SnapshotContext& ctx) const override {
        const auto a = allocation();
        const auto matte_bg = nk::Color{0.02F, 0.03F, 0.04F, 1.0F};

        nk::Rect matte = {
            a.x,
            a.y,
            std::max(0.0F, a.width),
            std::max(0.0F, a.height),
        };
        ctx.add_color_rect(matte, matte_bg);

        if (pixels_.empty() || src_width_ <= 0 || src_height_ <= 0) {
            const nk::FontDescriptor title_font{
                .family = {},
                .size = 24.0F,
                .weight = nk::FontWeight::Medium,
            };
            const nk::FontDescriptor body_font{
                .family = {},
                .size = 14.0F,
                .weight = nk::FontWeight::Regular,
            };
            const auto title_size = measure_text("Open a ROM", title_font);
            const auto body_size = measure_text("Use File > Open to load a game.", body_font);
            const float block_height = title_size.height + 10.0F + body_size.height;
            const float title_x = matte.x + std::max(0.0F, (matte.width - title_size.width) * 0.5F);
            const float title_y = matte.y + std::max(0.0F, (matte.height - block_height) * 0.5F);
            const float body_x = matte.x + std::max(0.0F, (matte.width - body_size.width) * 0.5F);
            const float body_y = title_y + title_size.height + 10.0F;

            ctx.add_text(
                {title_x, title_y}, "Open a ROM", nk::Color{0.78F, 0.81F, 0.86F, 1.0F}, title_font);
            ctx.add_text({body_x, body_y},
                         "Use File > Open to load a game.",
                         nk::Color{0.56F, 0.60F, 0.66F, 1.0F},
                         body_font);
            return;
        }

        constexpr float presentation_aspect = 4.0F / 3.0F;
        float image_width = matte.width;
        float image_height = image_width / presentation_aspect;
        if (image_height > matte.height) {
            image_height = matte.height;
            image_width = image_height * presentation_aspect;
        }
        nk::Rect image_rect = {
            matte.x + std::max(0.0F, (matte.width - image_width) * 0.5F),
            matte.y + std::max(0.0F, (matte.height - image_height) * 0.5F),
            image_width,
            image_height,
        };

        ctx.add_color_rect(image_rect, matte_bg);
        ctx.push_container(image_rect);
        ctx.add_image(image_rect, pixels_.data(), src_width_, src_height_, scale_mode_);
        ctx.pop_container();
    }

  private:
    PreviewCanvas() = default;

    std::vector<uint32_t> pixels_;
    int src_width_ = 0;
    int src_height_ = 0;
    nk::ScaleMode scale_mode_ = nk::ScaleMode::NearestNeighbor;
};

class HeroBanner : public nk::Widget {
  public:
    static std::shared_ptr<HeroBanner> create(std::string title,
                                              std::string subtitle,
                                              std::vector<std::shared_ptr<nk::Widget>> pills) {
        auto banner = std::shared_ptr<HeroBanner>(new HeroBanner());
        banner->set_horizontal_size_policy(nk::SizePolicy::Expanding);
        banner->title_ = std::move(title);
        banner->subtitle_ = std::move(subtitle);
        banner->set_pills(std::move(pills));
        return banner;
    }

    void set_pills(std::vector<std::shared_ptr<nk::Widget>> pills) {
        for (const auto& pill : pills_) {
            if (pill) {
                remove_child(*pill);
            }
        }

        pills_ = std::move(pills);
        for (const auto& pill : pills_) {
            if (pill) {
                append_child(pill);
            }
        }
        queue_layout();
    }

    [[nodiscard]] nk::SizeRequest measure(const nk::Constraints& constraints) const override {
        if (!cached_title_size_) {
            cached_title_size_ = measure_text(title_, title_font());
        }
        if (!cached_subtitle_size_) {
            cached_subtitle_size_ = measure_text(subtitle_, subtitle_font());
        }
        const auto title_size = *cached_title_size_;
        const auto subtitle_size = *cached_subtitle_size_;
        const float left_width = std::max(title_size.width, subtitle_size.width);
        const float left_height = title_size.height + 10.0F + subtitle_size.height;

        float pills_width = 0.0F;
        float pills_height = 0.0F;
        for (std::size_t index = 0; index < pills_.size(); ++index) {
            if (!pills_[index]) {
                continue;
            }
            const auto req = pills_[index]->measure(constraints);
            pills_width += req.natural_width;
            pills_height = std::max(pills_height, req.natural_height);
            if (index + 1 < pills_.size()) {
                pills_width += 10.0F;
            }
        }

        const float content_width = left_width + (pills_width > 0.0F ? pills_width + 28.0F : 0.0F);
        const float content_height = std::max(left_height, pills_height);
        return {
            480.0F,
            120.0F,
            std::max(660.0F, content_width + 52.0F),
            std::max(132.0F, content_height + 46.0F),
        };
    }

    void allocate(const nk::Rect& allocation) override {
        Widget::allocate(allocation);
        if (pills_.empty()) {
            return;
        }

        std::vector<nk::SizeRequest> requests;
        requests.reserve(pills_.size());
        float pills_width = 0.0F;
        float pills_height = 0.0F;
        for (const auto& pill : pills_) {
            if (!pill) {
                requests.push_back({});
                continue;
            }
            const auto req = pill->measure(nk::Constraints::tight(allocation.size()));
            requests.push_back(req);
            pills_width += req.natural_width;
            pills_height = std::max(pills_height, req.natural_height);
        }
        if (pills_.size() > 1) {
            pills_width += 10.0F * static_cast<float>(pills_.size() - 1);
        }

        float x = allocation.right() - 30.0F - pills_width;
        const float y = allocation.y + std::max(0.0F, (allocation.height - pills_height) * 0.5F);
        for (std::size_t index = 0; index < pills_.size(); ++index) {
            if (!pills_[index]) {
                continue;
            }
            pills_[index]->allocate(
                {x, y, requests[index].natural_width, requests[index].natural_height});
            x += requests[index].natural_width + 10.0F;
        }
    }

  protected:
    void snapshot(nk::SnapshotContext& ctx) const override {
        const auto a = allocation();
        const auto accent = nk::Color{0.15F, 0.48F, 0.47F, 1.0F};

        ctx.add_rounded_rect(
            {a.x, a.y + 1.0F, a.width, a.height}, nk::Color{0.08F, 0.12F, 0.18F, 0.04F}, 19.0F);
        ctx.add_rounded_rect(a, nk::Color{0.98F, 0.99F, 1.0F, 1.0F}, 18.0F);
        ctx.add_border(a, nk::Color{0.85F, 0.88F, 0.92F, 1.0F}, 1.0F, 18.0F);
        ctx.add_rounded_rect({a.x + 22.0F, a.y + 18.0F, 54.0F, 4.0F},
                             nk::Color{accent.r, accent.g, accent.b, 0.18F},
                             2.0F);

        if (!cached_title_size_) {
            cached_title_size_ = measure_text(title_, title_font());
        }
        if (!cached_subtitle_size_) {
            cached_subtitle_size_ = measure_text(subtitle_, subtitle_font());
        }

        ctx.add_text(
            {a.x + 22.0F, a.y + 36.0F}, title_, nk::Color{0.12F, 0.14F, 0.17F, 1.0F}, title_font());
        ctx.add_text({a.x + 22.0F, a.y + 36.0F + cached_title_size_->height + 10.0F},
                     subtitle_,
                     nk::Color{0.36F, 0.40F, 0.45F, 1.0F},
                     subtitle_font());

        if (!pills_.empty()) {
            bool have_bounds = false;
            nk::Rect pill_bounds{};
            for (const auto& pill : pills_) {
                if (!pill) {
                    continue;
                }
                const auto r = pill->allocation();
                if (!have_bounds) {
                    pill_bounds = r;
                    have_bounds = true;
                    continue;
                }
                const float left = std::min(pill_bounds.x, r.x);
                const float top = std::min(pill_bounds.y, r.y);
                const float right = std::max(pill_bounds.right(), r.right());
                const float bottom = std::max(pill_bounds.bottom(), r.bottom());
                pill_bounds = {left, top, right - left, bottom - top};
            }

            if (have_bounds) {
                nk::Rect tray = {
                    pill_bounds.x - 10.0F,
                    pill_bounds.y - 8.0F,
                    pill_bounds.width + 20.0F,
                    pill_bounds.height + 16.0F,
                };
                ctx.add_rounded_rect(
                    tray, nk::Color{0.95F, 0.97F, 0.985F, 1.0F}, tray.height * 0.5F);
                ctx.add_border(
                    tray, nk::Color{0.86F, 0.89F, 0.92F, 1.0F}, 1.0F, tray.height * 0.5F);
            }
        }
    }

  private:
    HeroBanner() = default;

    static nk::FontDescriptor title_font() {
        return {
            .family = {},
            .size = 28.0F,
            .weight = nk::FontWeight::Medium,
        };
    }

    static nk::FontDescriptor subtitle_font() {
        return {
            .family = {},
            .size = 14.0F,
            .weight = nk::FontWeight::Regular,
        };
    }

    std::string title_;
    std::string subtitle_;
    std::vector<std::shared_ptr<nk::Widget>> pills_;
    mutable std::optional<nk::Size> cached_title_size_;
    mutable std::optional<nk::Size> cached_subtitle_size_;
};

} // namespace mapperbus::frontend
