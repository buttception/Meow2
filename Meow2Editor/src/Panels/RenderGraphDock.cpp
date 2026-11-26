/// @file
/// @brief RenderGraphDock (RenderGraphDock.h).

#include "Panels/RenderGraphDock.h"

#include "Panels/FlowView.h"
#include "Viewports/CaptureTools.h"

#include <format>
#include <QByteArray>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QImage>
#include <QJsonArray>
#include <QLabel>
#include <QListWidget>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QToolButton>
#include <QVBoxLayout>

namespace Meow2::Editor
{
using namespace Qt::StringLiterals;

RenderGraphDock::RenderGraphDock(EditorCommandRegistry& commands, Log::Logger& log, QWidget* parent)
    : QDockWidget(u"Render graph"_s, parent)
    , m_commands(commands)
    , m_log(log)
{
    setObjectName(u"RenderGraphDock"_s); // QMainWindow::saveState() keys docks by object name
    auto* content = new QWidget();
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(6, 6, 6, 6);

    auto* bar = new QHBoxLayout();
    auto* capture = new QPushButton(u"Capture frame (F9)"_s, content);
    capture->setObjectName(u"CaptureFrame"_s);
    capture->setToolTip(u"Keeps the next frame: every pass, and every texture as each pass left it. RenderDoc is "
                        u"for keeping captures."_s);
    connect(capture, &QPushButton::clicked, this, [this] { run(u"capture.frame"_s, {}); });
    auto* clear = new QPushButton(u"Clear"_s, content);
    clear->setObjectName(u"CaptureClear"_s);
    connect(clear, &QPushButton::clicked, this, [this] { run(u"capture.clear"_s, {}); });
    m_status = new QLabel(content);
    m_status->setObjectName(u"CaptureStatus"_s);
    m_status->setStyleSheet(u"color: #8A8A90;"_s);
    bar->addWidget(capture);
    bar->addWidget(clear);
    bar->addWidget(m_status, 1);
    layout->addLayout(bar);

    auto* splitter = new QSplitter(Qt::Vertical, content);
    auto* scroll = new QScrollArea(splitter);
    m_view = new FlowView(scroll);
    m_view->setObjectName(u"FlowView"_s);
    scroll->setWidget(m_view);
    scroll->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_view->onPassClicked([this](std::uint32_t pass) { selectPass(pass); });
    m_view->onPinClicked([this](const Renderer::FlowPin& pin) { selectPin(pin); });
    splitter->addWidget(scroll);

    auto* details = new QWidget(splitter);
    auto* detailsLayout = new QHBoxLayout(details);
    detailsLayout->setContentsMargins(0, 0, 0, 0);
    auto* passColumn = new QVBoxLayout();
    m_passTitle = new QLabel(u"Click a pass or a pin."_s, details);
    m_passTitle->setObjectName(u"PassTitle"_s);
    m_passTitle->setStyleSheet(u"font-weight: 600;"_s);
    m_accesses = new QListWidget(details);
    m_accesses->setObjectName(u"PassAccesses"_s);
    connect(m_accesses, &QListWidget::itemClicked, this,
            [this](QListWidgetItem* item)
            {
                const QVariant writer = item->data(Qt::UserRole);
                if (writer.isValid())
                {
                    showTexture(writer.toUInt(), item->data(Qt::UserRole + 1).toUInt());
                }
            });
    passColumn->addWidget(m_passTitle);
    passColumn->addWidget(m_accesses, 1);
    detailsLayout->addLayout(passColumn, 1);

    auto* textureColumn = new QVBoxLayout();
    m_textureTitle = new QLabel(details);
    m_textureTitle->setObjectName(u"TextureTitle"_s);
    textureColumn->addWidget(m_textureTitle);
    auto* controls = new QHBoxLayout();
    const std::array<QString, 4> names{u"R"_s, u"G"_s, u"B"_s, u"A"_s};
    for (std::size_t i = 0; i < names.size(); ++i)
    {
        auto* channel = new QToolButton(details);
        channel->setObjectName(u"Channel."_s + names[i]);
        channel->setText(names[i]);
        channel->setCheckable(true);
        connect(channel, &QToolButton::clicked, this, [this] { requestTexture(); });
        controls->addWidget(channel);
        m_channels[i] = channel;
    }
    const auto rangeBox = [details](const QString& name)
    {
        auto* box = new QDoubleSpinBox(details);
        box->setObjectName(name);
        box->setRange(-1e6, 1e6);
        box->setDecimals(4);
        box->setSingleStep(0.01);
        box->setKeyboardTracking(false); // a picture per finished edit, not per key
        return box;
    };
    m_rangeMin = rangeBox(u"RangeMin"_s);
    m_rangeMax = rangeBox(u"RangeMax"_s);
    connect(m_rangeMin, &QDoubleSpinBox::editingFinished, this, [this] { requestTexture(); });
    connect(m_rangeMax, &QDoubleSpinBox::editingFinished, this, [this] { requestTexture(); });
    controls->addWidget(new QLabel(u"Range"_s, details));
    controls->addWidget(m_rangeMin);
    controls->addWidget(m_rangeMax);
    controls->addStretch(1);
    textureColumn->addLayout(controls);
    m_preview = new QLabel(details);
    m_preview->setObjectName(u"CapturePreview"_s);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setMinimumSize(160, 90);
    m_preview->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    textureColumn->addWidget(m_preview, 1);
    detailsLayout->addLayout(textureColumn, 2);
    splitter->addWidget(details);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    layout->addWidget(splitter, 1);
    setWidget(content);
    m_status->setText(u"No capture. Capture frame (F9) keeps one frame in memory."_s);
}

void RenderGraphDock::run(const QString& command, const QJsonObject& parameters)
{
    if (const Result<QJsonValue> ran = m_commands.execute(command, parameters); !ran)
    {
        MEOW2_LOG_WARN(m_log, "{}: {}", command.toStdString(), ran.error());
    }
}

void RenderGraphDock::refresh()
{
    const Result<QJsonValue> brief = m_commands.execute(u"capture.info"_s, {{u"brief"_s, true}});
    if (!brief)
    {
        return;
    }
    const QJsonObject state = brief->toObject();
    const double frame = state.value(u"captured").toBool() ? state.value(u"frame").toDouble() : -1.0;
    if (frame != m_shownFrame)
    {
        // A new capture (or none): the graph again, the selection gone.
        m_shownFrame = frame;
        m_pass.reset();
        m_texturePass.reset();
        m_accesses->clear();
        m_passTitle->setText(u"Click a pass or a pin."_s);
        m_textureTitle->clear();
        m_preview->clear();
        if (frame < 0.0)
        {
            m_view->setFlow({});
            m_status->setText(u"No capture. Capture frame (F9) keeps one frame in memory."_s);
        }
        else if (const Result<QJsonValue> full = m_commands.execute(u"capture.info"_s, {}); full)
        {
            const QJsonObject info = full->toObject();
            m_view->setFlow(flowFromJson(info.value(u"flow").toObject()));
            m_status->setText(u"Frame %1: %2 passes, %3 textures kept."_s.arg(static_cast<qint64>(frame))
                                  .arg(m_view->flow().passes.size())
                                  .arg(info.value(u"snapshots").toArray().size()));
        }
    }
    if (state.value(u"pending").toBool())
    {
        m_status->setText(u"Capturing the next frame…"_s);
    }

    const Result<QJsonValue> image = m_commands.execute(u"capture.image"_s, {});
    if (!image)
    {
        return;
    }
    const QJsonObject picture = image->toObject();
    if (picture.value(u"serial").toDouble() == m_imageSerial)
    {
        return;
    }
    m_imageSerial = picture.value(u"serial").toDouble();
    if (!picture.value(u"ready").toBool())
    {
        m_preview->clear();
        return;
    }
    if (const Result<QJsonValue> withPng = m_commands.execute(u"capture.image"_s, {{u"data"_s, true}}); withPng)
    {
        const QByteArray png = QByteArray::fromBase64(withPng->toObject().value(u"png").toString().toLatin1());
        QImage decoded;
        if (decoded.loadFromData(png, "PNG"))
        {
            m_preview->setPixmap(
                QPixmap::fromImage(decoded).scaled(m_preview->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
            m_preview->setToolTip(
                u"%1 × %2 (shrunk to fit; read back from the GPU)"_s.arg(decoded.width()).arg(decoded.height()));
        }
    }
}

void RenderGraphDock::selectPass(std::uint32_t pass)
{
    if (pass >= m_view->flow().passes.size())
    {
        return;
    }
    m_pass = pass;
    m_view->setSelection(pass, std::nullopt);
    listPass();
}

void RenderGraphDock::selectPin(const Renderer::FlowPin& pin)
{
    const Renderer::FrameFlow& flow = m_view->flow();
    if (pin.pass >= flow.passes.size() || pin.resource >= flow.resources.size())
    {
        return;
    }
    m_pass = pin.pass;
    m_view->setSelection(pin.pass, pin);
    listPass();
    // An output shows what this pass wrote; an input, what the pass before it left there.
    const std::optional<std::uint32_t> writer =
        pin.output ? std::optional{pin.pass} : Renderer::writerBefore(flow, pin.resource, pin.pass);
    if (writer && flow.resources[pin.resource].texture)
    {
        showTexture(*writer, pin.resource);
    }
}

void RenderGraphDock::listPass()
{
    const Renderer::FrameFlow& flow = m_view->flow();
    m_accesses->clear();
    if (!m_pass)
    {
        return;
    }
    const Renderer::FlowPass& pass = flow.passes[*m_pass];
    m_passTitle->setText(u"%1  %2%3"_s.arg(*m_pass)
                             .arg(QString::fromStdString(pass.name))
                             .arg(pass.skipped ? u" (cached)"_s : QString{}));
    for (const Renderer::FlowAccess& access : pass.accesses)
    {
        const Renderer::FlowResource& resource = flow.resources[access.resource];
        const std::optional<std::uint32_t> writer =
            access.writes ? std::optional{*m_pass} : Renderer::writerBefore(flow, access.resource, *m_pass);
        std::string label = std::format("{} {}  ({})", access.writes ? "OUT" : "IN ", resource.name,
                                        Renderer::resourceStateName(access.state));
        if (resource.texture)
        {
            label += std::format("  {}x{} {}", resource.info.width, resource.info.height,
                                 Renderer::formatName(resource.info.format));
        }
        if (!access.writes)
        {
            label += writer ? std::format("  from {}", flow.passes[*writer].name) : "  (from before the frame)";
        }
        auto* item = new QListWidgetItem(QString::fromStdString(label), m_accesses); // the list owns it
        item->setForeground(flowKindColor(Renderer::resourceKind(flow, access.resource)));
        if (resource.texture && writer)
        {
            item->setData(Qt::UserRole, *writer);
            item->setData(Qt::UserRole + 1, access.resource);
            item->setToolTip(u"Click to see it as that pass left it."_s);
        }
    }
}

void RenderGraphDock::showTexture(std::uint32_t pass, std::uint32_t resource)
{
    const Renderer::FrameFlow& flow = m_view->flow();
    m_texturePass = pass;
    m_textureResource = resource;
    m_textureTitle->setText(u"%1 after %2"_s.arg(QString::fromStdString(flow.resources[resource].name),
                                                 QString::fromStdString(flow.passes[pass].name)));
    // Start from how such a texture is best seen: depth as one grey channel over the near range, colour as it is.
    const Renderer::TextureDisplaySettings settings =
        Renderer::defaultDisplaySettings(Renderer::isDepthFormat(flow.resources[resource].info.format));
    for (std::size_t i = 0; i < m_channels.size(); ++i)
    {
        m_channels[i]->setChecked((settings.channels & (1U << i)) != 0);
    }
    m_rangeMin->setValue(static_cast<double>(settings.rangeMin));
    m_rangeMax->setValue(static_cast<double>(settings.rangeMax));
    requestTexture();
}

void RenderGraphDock::requestTexture()
{
    if (!m_texturePass)
    {
        return;
    }
    std::uint32_t channels = 0;
    for (std::size_t i = 0; i < m_channels.size(); ++i)
    {
        channels |= m_channels[i]->isChecked() ? (1U << i) : 0U;
    }
    if (channels == 0)
    {
        channels = 0b0111; // nothing ticked shows nothing: colour as it is instead
    }
    run(u"capture.show"_s, {{u"pass"_s, static_cast<double>(*m_texturePass)},
                            {u"resource"_s, static_cast<double>(m_textureResource)},
                            {u"channels"_s, static_cast<double>(channels)},
                            {u"rangeMin"_s, m_rangeMin->value()},
                            {u"rangeMax"_s, std::max(m_rangeMax->value(), m_rangeMin->value() + 1e-6)}});
}
} // namespace Meow2::Editor
