## Getting started

Cube is a dynamically typed, object oriented, programming language written in C. The language is based on the [Lox](http://craftinginterpreters.com/the-lox-language.html){:target="_blank"} programming language, from the book [Crafting Interpreters](http://craftinginterpreters.com){:target="_blank"} by Bob Nystrom.

### Features

Cube has a large set of features, making it a complete programming language. The current features includes:

* Primitive variables
* Lists and dicts
* Loops and conditions
* Functions
* Classes
* Imports
* Files
* Closure context
* Asynchronous features
* Bytecode VM

### Modes

Cube supports running in an interactive mode and from code files. On calling it without a file argument the interactive mode will start and any code can be typed directly in the console. If the file argument is present then this code is loaded and executed. 

Cube files can be compiled into a bytecode form by passing the '-c' argument. To make the code debuggable the argument '-d' must be passed. Any other argument after any set of there are passed as arguments to the cube program being executed.

### Let's start coding

As you expect from any programming language in the world, you can say hello!

```javascript
print('Hello, world!');
```

In cube you can also do any complex thing you can think of:

```javascript
for(var i in 'A'..'Z') println('Letter: ', i);
```

### What's next

From here you are already a Cube Programmer. You can start venturing in the Cube world by learning the language. If you ever run into bugs, or have any ideas or questions, you can:

Open an [issue](https://github.com/AlexanderSilvaB/cube/issues){:target="_blank"} on [Github](https://github.com/AlexanderSilvaB/cube){:target="_blank"}.

Send a [pull request](https://github.com/AlexanderSilvaB/cube/pulls){:target="_blank"}.

Email me at [alexander.ti.ufv@gmail.com](mailto:alexander.ti.ufv@gmail.com?subject=[Github]%20Cube).