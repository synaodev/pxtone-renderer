#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <vector>

#include "encoder.h"
#include "global.h"
#include "pxtone/pxtn.h"
#include "pxtone/pxtnService.h"

typedef std::pair<std::string, std::string> Arg;
struct KnownArg {
  bool empty() {
    for (auto it : keyMatches)
      if (!it.empty()) return false;
    return true;
  }
  std::set<std::string> keyMatches;
  bool takesParameter = false;
};

static const KnownArg argHelp = {{"--help", "-h"}}, argStdout = {{"--stdout"}},
                      argFormat = {{"--format", "-f"}, true};
static const std::vector<KnownArg> knownArguments = {argHelp, argStdout,
                                                     argFormat};

KnownArg findArgument(const std::string &key) {
  KnownArg match;
  for (auto it : knownArguments) {
    auto arg = it.keyMatches.find(key);
    if (arg != it.keyMatches.end()) {
      match = it;
    }
  }
  return match;
}

static std::set<std::filesystem::path> files;
bool parseArguments(const std::vector<std::string> &args) {
  //  for (auto it : args) std::cout << "'" << it << "'" << std::endl;
  std::map<std::string, std::string> argData;

  bool waitingForSecond = true;
  for (auto it = args.begin(); it != args.end(); ++it) {
    Arg arg;
    while (waitingForSecond) {
      if (arg.first.empty()) {
        if (*it->begin() == '-') {
          auto match = findArgument(*it);
          if (!match.empty()) {
            arg.first = *it;
            waitingForSecond = match.takesParameter;
          } else
            return logToConsole("Unknown argument '" + *it + "'");
        } else {
          files.insert(*it);
          waitingForSecond = false;
        }
      } else {
        if (it != args.end()) ++it;
        if (*it->begin() == '-')
          return logToConsole("Argument '" + arg.first +
                              "' requires a parameter.");
        else {
          arg.second = *it;
          waitingForSecond = false;
        }
      }
    }
    waitingForSecond = true;
    argData.insert(arg);
  }

  for (auto it : argHelp.keyMatches) {
    auto helpFound = argData.find(it);
    if (helpFound == argData.end())
      continue;
    else
      return logToConsole();
  }

  for (auto it : argStdout.keyMatches) {
    auto stdoutFound = argData.find(it);
    if (stdoutFound == argData.end())
      continue;
    else {
      if (files.size() > 1)
        return logToConsole(
            "Standard output cannot be used when rendering multiple files.");
      else
        config.toStdout = true;
    }
  }

  for (auto it : argFormat.keyMatches) {
    auto formatFound = argData.find(it);
    if (formatFound == argData.end() || formatFound->second.empty())
      continue;
    else {
      auto str = formatFound->second;
      std::transform(str.begin(), str.end(), str.begin(),
                     [](unsigned char c) { return std::toupper(c); });
      str == "OGG"   ? config.format = Config::OGG
      : str == "WAV" ? config.format = Config::WAV
      : str == "FLAC"
          ? config.format = Config::WAV
          : logToConsole("Unknown format type '" + formatFound->second +
                             "'; Resorting to OGG",
                         LogState::Warning);
    }
  }
  return true;
}

void decode(std::filesystem::path file) {
  FILE *fp = fopen(file.string().c_str(), "rb");
  if (fp == nullptr)
    throw GetError::file("Error opening file " + file.string() +
                         ". The file may not be readable to your user.");
  fseek(fp, 0L, SEEK_END);
  size_t size = static_cast<size_t>(ftell(fp));
  unsigned char *data = static_cast<unsigned char *>(malloc(size));
  if (data == nullptr)
    throw GetError::generic("Error allocating " + std::to_string(size) +
                            " bytes.");
  rewind(fp);
  if (fread(data, 1, size, fp) != size)
    throw GetError::file("Bytes read does not match file size.");
  fclose(fp);

  pxtnService *pxtn = new pxtnService();

  auto err = pxtn->init();
  if (err != pxtnOK) throw GetError::pxtone(err);
  if (!pxtn->set_destination_quality(CHANNEL_COUNT, SAMPLE_RATE))
    throw GetError::pxtone(
        "Could not set destination quality: " + std::to_string(CHANNEL_COUNT) +
        " channels, " + std::to_string(SAMPLE_RATE) + "Hz.");

  pxtnDescriptor desc;
  if (!desc.set_memory_r(static_cast<void *>(data), static_cast<int>(size)))
    throw GetError::pxtone("Could not set pxtone memory blob of size " +
                           std::to_string(size));
  err = pxtn->read(&desc);
  if (err != pxtnOK) throw GetError::pxtone(err);
  err = pxtn->tones_ready();
  if (err != pxtnOK) throw GetError::pxtone(err);

  pxtnVOMITPREPARATION prep{};
  prep.flags |= pxtnVOMITPREPFLAG_loop |
                pxtnVOMITPREPFLAG_unit_mute;  // TODO: figure this out
  prep.start_pos_sample = 0;
  prep.master_volume = 0.8f;  // this is probably good

  if (!pxtn->moo_preparation(&prep))
    throw GetError::pxtone("I Have No Mouth, and I Must Moo");

  pxtn->evels->Release();

  logToConsole("Successfully opened file " + file.filename().string() + ", " +
                   std::to_string(size) + " bytes read.",
               LogState::Info);

  // now decode lol
}

int main(int argc, char *argv[]) {
  if (argc <= 1) return logToConsole("At least 1 .ptcop file is required.");

  std::vector<std::string> args;
  for (int i = 1; i < argc; i++) {
    std::string str = argv[i];
    std::replace(str.begin(), str.end(), '=', ' ');
    std::stringstream ss(str);
    while (getline(ss, str, ' ')) args.push_back(str);
  }

  if (!parseArguments(args)) return 0;

  for (auto it : files) {
    auto absolute = std::filesystem::absolute(it);
    if (std::filesystem::exists(it)) try {
        decode(std::filesystem::absolute(it));
      } catch (std::string err) {
        logToConsole(err);
      }
    else
      logToConsole("File " + std::string(it) + " not found.",
                   LogState::Warning);
  }
  return 0;
}