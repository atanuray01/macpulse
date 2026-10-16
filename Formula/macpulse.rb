class Macpulse < Formula
  desc "Ultra-fast Apple Silicon system monitor & dashboard for macOS terminal"
  homepage "https://github.com/atanuray/macpulse"
  url "https://github.com/atanuray/macpulse/archive/refs/tags/v1.0.0.tar.gz"
  license "MIT"
  head "https://github.com/atanuray/macpulse.git", branch: "main"

  depends_on :macos
  depends_on arch: :arm64

  def install
    system "make", "build"
    bin.install "bin/macpulse"
    (pkgshare/"themes").install Dir["themes/*.json"]
  end

  def post_install
    theme_dir = "#{ENV["HOME"]}/.config/macpulse/themes"
    mkdir_p theme_dir
    cp Dir["#{pkgshare}/themes/*.json"], theme_dir
  end

  test do
    assert_match "MacPulse v", shell_output("#{bin}/macpulse --version")
    assert_match "Matrix Hacker", shell_output("#{bin}/macpulse --list-themes")
  end
end
