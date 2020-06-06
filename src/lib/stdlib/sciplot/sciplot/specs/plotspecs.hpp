// sciplot - a modern C++ scientific plotting library powered by gnuplot
// https://github.com/sciplot/sciplot
//
// Licensed under the MIT License <http://opensource.org/licenses/MIT>.
//
// Copyright (c) 2018 Allan Leal
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

// sciplot includes
#include <sciplot/enums.hpp>
#include <sciplot/specs/linespecs.hpp>
#include <sciplot/util.hpp>

namespace sciplot {
namespace internal {

/// The class where options for the plot function are specified.
class plotspecs : public linespecs<plotspecs>
{
public:
    /// Construct a plotspecs instance.
    /// @param what A string representing what to be plot (e.g., "'filename' u 1:2", "sin(x)", etc.)
    plotspecs(std::string what);

    /// Convert this plotspecs object into a gnuplot formatted string.
    auto repr() const -> std::string;

    /// Set the title of the plot.
    auto title(std::string value) -> plotspecs& { m_title = titlestr(value); return *this; }

    /// Set the format of the plot (lines, points, linespoints).
    auto with(style value) -> plotspecs& { m_with = stylestr(value); return *this; }

private:
    /// The what to be plotted as a gnuplot formatted string (e.g., "sin(x)").
    std::string m_what;

    /// The title of the plot as a gnuplot formatted string (e.g., "title 'sin(x)'").
    std::string m_title;

    /// The style of the plot (lines, points, linespoints) as a gnuplot formatted string (e.g., "with linespoints").
    std::string m_with;

    /// The line style of the plot as a gnuplot formatted string (e.g., "ls 2").
    std::string m_linestyle;

    /// The line type of the plot as a gnuplot formatted string (e.g., "lt 3").
    std::string m_linetype;

    /// The line width of the plot as a gnuplot formatted string (e.g., "lw 2").
    std::string m_linewidth;

    /// The line color of the plot as a gnuplot formatted string (e.g., "lc rgb '#FF00FF'").
    std::string m_linecolor;

    /// The dash type of the plot as a gnuplot formatted string (e.g., "dt 2").
    std::string m_dashtype;
};

plotspecs::plotspecs(std::string what) : m_what(what)
{
    with(DEFAULT_LINESTYLE);
}

auto plotspecs::repr() const -> std::string
{
    std::stringstream ss;
    ss << m_what << " ";
    ss << optionvaluestr("title", m_title);
    ss << optionvaluestr("with", m_with);
    ss << linespecs<plotspecs>::repr();
    return ss.str();
}

} // namespace internal
} // namespace sciplot
