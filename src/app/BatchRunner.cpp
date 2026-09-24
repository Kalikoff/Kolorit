#include "app/BatchRunner.h"

#include "core/BuiltinPalettes.h"
#include "core/ColorSpace.h"
#include "core/Palette.h"
#include "processing/PaletteMapper.h"

#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QPromise>
#include <QSet>
#include <QTextStream>

#include <cstdio>

// The command-line tool speaks English only and loads no translation: builtin palette
// names are matched against their untranslated form, and scripts should not break
// when they run under a different system language.

namespace kolorit::batch {

namespace {

QTextStream& out()
{
    static QTextStream stream(stdout);
    return stream;
}

QTextStream& err()
{
    static QTextStream stream(stderr);
    return stream;
}

/// Color model name in a form suitable for the command line
QString modelKey(ColorModel model)
{
    return colorspace::modelName(model).remove(QLatin1String("CIE ")).toLower();
}

/// Short names of the dithering methods, used to pick them on the command line
QString ditherKey(Dithering d)
{
    switch (d) {
    case Dithering::None:              return QStringLiteral("none");
    case Dithering::Bayer2:            return QStringLiteral("bayer2");
    case Dithering::Bayer4:            return QStringLiteral("bayer4");
    case Dithering::Bayer8:            return QStringLiteral("bayer8");
    case Dithering::BlueNoise:         return QStringLiteral("bluenoise");
    case Dithering::FloydSteinberg:    return QStringLiteral("floyd");
    case Dithering::Atkinson:          return QStringLiteral("atkinson");
    case Dithering::Sierra:            return QStringLiteral("sierra");
    case Dithering::Stucki:            return QStringLiteral("stucki");
    case Dithering::JarvisJudiceNinke: return QStringLiteral("jarvis");
    }
    return QStringLiteral("none");
}

QStringList keysOf(const QList<Dithering>& list)
{
    QStringList keys;
    for (Dithering d : list)
        keys << ditherKey(d);
    return keys;
}

QStringList modelKeys()
{
    QStringList keys;
    for (ColorModel m : colorspace::allModels())
        keys << modelKey(m);
    return keys;
}

/// Palette from a file or from the builtin ones; names are compared case-insensitively
Palette resolvePalette(const QString& spec, QString* error)
{
    if (QFileInfo::exists(spec)) {
        QString message;
        const Palette palette = Palette::load(spec, &message);
        if (palette.isEmpty())
            *error = message.isEmpty() ? QStringLiteral("the palette file is empty") : message;
        return palette;
    }
    for (const builtin::BuiltinPalette& entry : builtin::palettes())
        if (entry.name.compare(spec, Qt::CaseInsensitive) == 0)
            return entry.palette;

    *error = QStringLiteral("there is no file or builtin palette with this name (see --list-palettes)");
    return {};
}

/// Print the builtin palettes grouped the same way as in the window menu
void listPalettes()
{
    for (builtin::PaletteGroup group : builtin::allGroups()) {
        out() << builtin::groupName(group) << Qt::endl;
        for (const builtin::BuiltinPalette& entry : builtin::palettes(group))
            out() << QStringLiteral("  %1 (%2)").arg(entry.name).arg(entry.palette.size()) << Qt::endl;
    }
}

/// Expand the input paths: folders turn into the files of supported formats they contain
QStringList collectInputs(const QStringList& paths)
{
    QStringList formats;
    for (const QByteArray& f : QImageReader::supportedImageFormats())
        formats << QStringLiteral("*.") + QString::fromLatin1(f);

    QStringList files;
    for (const QString& path : paths) {
        const QFileInfo info(path);
        if (info.isDir()) {
            QDir dir(path);
            for (const QFileInfo& entry : dir.entryInfoList(formats, QDir::Files, QDir::Name))
                files << entry.absoluteFilePath();
        } else if (info.isFile()) {
            files << info.absoluteFilePath();
        } else {
            err() << QStringLiteral("  skipped: \"%1\" is not a file or folder").arg(path) << Qt::endl;
        }
    }
    files.removeDuplicates();
    return files;
}

} // namespace

bool isRequested(const QStringList& arguments)
{
    return arguments.contains(QStringLiteral("--batch"));
}

int run(const QStringList& arguments)
{
    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Kolorit: batch repainting of images into a palette, without the GUI."));
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption batchOption(QStringLiteral("batch"),
                                         QStringLiteral("Accepted for compatibility; the tool always runs in batch mode."));
    const QCommandLineOption listOption(QStringLiteral("list-palettes"),
                                        QStringLiteral("Print the builtin palettes and exit."));
    const QCommandLineOption paletteOption(QStringLiteral("palette"),
                                           QStringLiteral("Palette: path to an N×1 PNG or a builtin palette name."),
                                           QStringLiteral("file|name"));
    const QCommandLineOption outOption(QStringLiteral("out"), QStringLiteral("Folder for the results."),
                                       QStringLiteral("folder"));
    const QCommandLineOption modelOption(QStringLiteral("model"),
                                         QStringLiteral("Color model: %1. Default: lab.")
                                             .arg(modelKeys().join(QStringLiteral(", "))),
                                         QStringLiteral("model"), QStringLiteral("lab"));
    const QCommandLineOption ditherOption(QStringLiteral("dither"),
                                          QStringLiteral("Dithering: %1. Default: none.")
                                              .arg(keysOf(allDitherings()).join(QStringLiteral(", "))),
                                          QStringLiteral("method"), QStringLiteral("none"));
    const QCommandLineOption strengthOption(QStringLiteral("strength"),
                                            QStringLiteral("Strength of ordered dithering, 0..128."),
                                            QStringLiteral("number"), QStringLiteral("32"));
    const QCommandLineOption formatOption(QStringLiteral("format"),
                                          QStringLiteral("Output format. Default: png."),
                                          QStringLiteral("png|jpg|webp|bmp"), QStringLiteral("png"));
    parser.addOptions({batchOption, listOption, paletteOption, outOption, modelOption, ditherOption,
                       strengthOption, formatOption});
    parser.addPositionalArgument(QStringLiteral("input"), QStringLiteral("Image files or folders."),
                                 QStringLiteral("[input...]"));
    parser.process(arguments);

    if (parser.isSet(listOption)) {
        listPalettes();
        return 0;
    }

    // --- Argument checks ---
    const QStringList inputPaths = parser.positionalArguments();
    if (inputPaths.isEmpty()) {
        err() << QStringLiteral("No input files or folders given.") << Qt::endl;
        return 2;
    }
    if (!parser.isSet(paletteOption)) {
        err() << QStringLiteral("No palette given (--palette).") << Qt::endl;
        return 2;
    }
    if (!parser.isSet(outOption)) {
        err() << QStringLiteral("No output folder given (--out).") << Qt::endl;
        return 2;
    }

    QString paletteError;
    const Palette palette = resolvePalette(parser.value(paletteOption), &paletteError);
    if (palette.isEmpty()) {
        err() << QStringLiteral("Palette \"%1\": %2").arg(parser.value(paletteOption), paletteError) << Qt::endl;
        return 2;
    }

    RepaintParams params;
    const QString wantedModel = parser.value(modelOption).toLower();
    bool modelFound = false;
    for (ColorModel m : colorspace::allModels())
        if (modelKey(m) == wantedModel) {
            params.model = m;
            modelFound = true;
        }
    if (!modelFound) {
        err() << QStringLiteral("Unknown model \"%1\". Available: %2")
                     .arg(wantedModel, modelKeys().join(QStringLiteral(", ")))
              << Qt::endl;
        return 2;
    }

    const QString wantedDither = parser.value(ditherOption).toLower();
    bool ditherFound = false;
    for (Dithering d : allDitherings())
        if (ditherKey(d) == wantedDither) {
            params.dithering = d;
            ditherFound = true;
        }
    if (!ditherFound) {
        err() << QStringLiteral("Unknown dithering \"%1\". Available: %2")
                     .arg(wantedDither, keysOf(allDitherings()).join(QStringLiteral(", ")))
              << Qt::endl;
        return 2;
    }
    params.ditherStrength = qBound(0, parser.value(strengthOption).toInt(), 128);

    const QString format = parser.value(formatOption).toLower();
    QDir outDir(parser.value(outOption));
    if (!outDir.mkpath(QStringLiteral("."))) {
        err() << QStringLiteral("Could not create the folder \"%1\".").arg(outDir.path()) << Qt::endl;
        return 2;
    }

    const QStringList files = collectInputs(inputPaths);
    if (files.isEmpty()) {
        err() << QStringLiteral("No images found.") << Qt::endl;
        return 2;
    }

    // --- Processing ---
    out() << QStringLiteral("Palette: %1 colors   model: %2   dithering: %3   files: %4")
                 .arg(palette.size())
                 .arg(colorspace::modelName(params.model), ditheringName(params.dithering))
                 .arg(files.size())
          << Qt::endl;

    const ImageOp op = PaletteMapper::makeOp(palette, params);
    int done = 0;
    int failed = 0;
    // Otherwise files with the same name from different folders overwrite each other
    QSet<QString> taken;
    QElapsedTimer total;
    total.start();

    for (const QString& file : files) {
        const QFileInfo info(file);
        const QImage source = colorspace::toSrgb(QImage(file));
        if (source.isNull()) {
            err() << QStringLiteral("  ! %1: could not be read").arg(info.fileName()) << Qt::endl;
            ++failed;
            continue;
        }

        QPromise<QImage> promise;
        promise.start();
        promise.setProgressRange(0, 100);
        QElapsedTimer timer;
        timer.start();
        const QImage result = op(source, promise);
        promise.finish();

        if (result.isNull()) {
            err() << QStringLiteral("  ! %1: processing failed").arg(info.fileName()) << Qt::endl;
            ++failed;
            continue;
        }

        QString base = info.completeBaseName();
        for (int n = 2; taken.contains(base.toLower()); ++n)
            base = info.completeBaseName() + QLatin1Char('_') + QString::number(n);
        taken.insert(base.toLower());
        const QString target = outDir.filePath(base + QLatin1Char('.') + format);
        if (!result.save(target)) {
            err() << QStringLiteral("  ! %1: could not save to \"%2\"").arg(info.fileName(), target) << Qt::endl;
            ++failed;
            continue;
        }
        ++done;
        out() << QStringLiteral("  %1 → %2 (%3×%4, %5 ms)")
                     .arg(info.fileName(), QFileInfo(target).fileName())
                     .arg(result.width())
                     .arg(result.height())
                     .arg(timer.elapsed())
              << Qt::endl;
    }

    out() << QStringLiteral("Done: %1 of %2 in %3 ms").arg(done).arg(files.size()).arg(total.elapsed()) << Qt::endl;
    out().flush();
    err().flush();
    return failed == 0 ? 0 : 1;
}

} // namespace kolorit::batch
