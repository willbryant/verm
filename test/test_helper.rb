require 'rubygems'
require 'test/unit'
require 'fileutils'
require 'byebug'
require File.expand_path(File.join(File.dirname(__FILE__), 'net_http_multipart_post'))
require File.expand_path(File.join(File.dirname(__FILE__), 'verm_spawner'))

verm_binary = File.join(File.dirname(__FILE__), '..', 'verm')
verm_data   = File.join(File.dirname(__FILE__), 'data')
mime_types_file = File.join(File.dirname(__FILE__), 'fixtures', 'mime.types')
captured_stderr_filename = File.join(File.dirname(__FILE__), 'tmp', 'captured_stderr') unless ENV['NO_CAPTURE_STDERR'].to_i > 0
FileUtils.mkdir_p(File.join(File.dirname(__FILE__), 'tmp'))
VERM_SPAWNER = VermSpawner.new(verm_binary, verm_data, :mime_types_file => mime_types_file)
REPLICATION_MASTER_VERM_SPAWNER = VermSpawner.new(verm_binary, "#{verm_data}_replica", :mime_types_file => mime_types_file, :port => VERM_SPAWNER.port + 1, :replicate_to => VERM_SPAWNER.host, :capture_stderr_in => captured_stderr_filename)

module Verm
  class TestCase < Test::Unit::TestCase
    undef_method :default_test if instance_methods.include? 'default_test' or
                                  instance_methods.include? :default_test

    def extra_spawner_options
      {}
    end
  
    def setup
      VERM_SPAWNER.setup(extra_spawner_options)
    end

    def teardown
      VERM_SPAWNER.teardown
    end
    
    def timeout
      10 # seconds
    end

    def repeatedly_wait_until
      (timeout*10).times do
        return if yield
        sleep 0.1
      end
      raise TimeoutError
    end
    

    def fixture_file_path(filename)
      File.join(File.dirname(__FILE__), 'fixtures', filename)
    end

    def copy_arbitrary_file_to(directory, extension, compressed: false, spawner: VERM_SPAWNER)
      @arbitrary_file = 'binary_file'
      @arbitrary_file += '.gz' if compressed
      @original_file = File.join(File.dirname(__FILE__), 'fixtures', @arbitrary_file)
      @subdirectory, @filename = 'IF', 'P8unS2JIuR6_UZI5pZ0lxWHhfvR2ocOcRAma_lEiA' # happens to be appropriate for test/fixtures/binary_file, but not relevant to the tests
      @filename = "#{@filename}.#{extension}" if extension
      @location = "/#{directory}/#{@subdirectory}/#{@filename}" # does not get .gz added, even if the file is compressed
      @filename += '.gz' if compressed

      @dest_directory = File.join(spawner.verm_data, directory)
      @dest_subdirectory = File.join(@dest_directory, @subdirectory)
      FileUtils.mkdir(@dest_directory) unless File.directory?(@dest_directory)
      FileUtils.mkdir(@dest_subdirectory) unless File.directory?(@dest_subdirectory)
      FileUtils.cp(@original_file, File.join(@dest_subdirectory, @filename))
    end

    def get(options)
      verm_spawner = options[:verm] || VERM_SPAWNER
      http = Net::HTTP.new(verm_spawner.hostname, verm_spawner.port)
      http.read_timeout = timeout
      
      request = Net::HTTP::Get.new(options[:path])
      options[:headers].each {|k, v| request[k] = v} if options[:headers]
      request['Accept-Encoding'] = options[:accept_encoding] # even if not set, write a nil to disable decode_content
      assert !request.decode_content, "disabling decode_content failed!"

      response = http.start do |connection|
        connection.request(request)
      end

      assert_equal options[:expected_response_code] || 200, response.code.to_i, "The response didn't have the expected code"
      assert_equal options[:expected_content_type], response.content_type, "The response had an incorrect content-type" if options.has_key?(:expected_content_type)
      assert_equal options[:expected_content_length], response.content_length, "The response had an incorrect content-length" if options.has_key?(:expected_content_length)
      assert_equal options[:expected_content_encoding], response['content-encoding'], "The response had an incorrect content-encoding" if options.has_key?(:expected_content_encoding)
      assert_equal options[:expected_content], response.body, "The response had incorrect content" if options.has_key?(:expected_content)
      
      response
    end

    def expected_filename(location, options = {})
      verm_spawner = options[:verm] || VERM_SPAWNER
      dest_filename = File.expand_path(File.join(verm_spawner.verm_data, location))
      dest_filename += '.' + options[:expected_extension_suffix] if options[:expected_extension_suffix]
      dest_filename
    end
    
    def post_file(options)
      verm_spawner = options[:verm] || VERM_SPAWNER
      orig_filename = fixture_file_path(options[:file])
      file_data = File.read(orig_filename, :mode => 'rb')
      
      if @multipart
        request = Net::HTTP::MultipartPost.new(options[:path])
        request.attach 'uploaded_file', file_data, options[:file], options[:type]
        request.form_data = {"test" => "bar"}
      else
        request = Net::HTTP::Post.new(options[:path])
        request.content_type = options[:type]
        request['Content-Encoding'] = options[:encoding] if options[:encoding]
      end
      request['Accept-Encoding'] = options[:accept_encoding] # even if not set, write a nil to disable decode_content
      assert !request.decode_content, "disabling decode_content failed!"
      
      http = Net::HTTP.new(verm_spawner.hostname, verm_spawner.port)
      http.read_timeout = timeout
      response = http.start do |connection|
        if @multipart
          connection.request(request)
        else
          connection.request(request, file_data)
        end
      end
      response.error! unless response.is_a?(Net::HTTPSuccess)
      assert_equal options[:expected_response_code] || 201, response.code.to_i, "The response didn't have the expected code (got #{response.code}, #{response.body.chomp})"

      location = response['location']
      raise "No location was returned, it was supposed to be saved under #{options[:path]}/" if location.nil?
      raise "The location returned was #{location}, but it was supposed to be saved under #{options[:path]}/" if location[0..options[:path].length] != "#{options[:path]}/"
      raise "The location returned was #{location}, but it was supposed to have a #{options[:expected_extension]} extension" if options[:expected_extension] && location[(-options[:expected_extension].length - 1)..-1] != ".#{options[:expected_extension]}"
      dest_filename = expected_filename(location, options)
      raise "Verm supposedly saved the file to #{dest_filename}, but that doesn't exist" unless File.exist?(dest_filename)
      saved_data = File.read(dest_filename, :mode => 'rb')
      raise "The data saved to file doesn't match the original! #{saved_data.inspect} vs. #{file_data.inspect}" unless saved_data == file_data
      location
    end
    
    def put(options, verm_spawner = VERM_SPAWNER)
      file_data = options[:data]
      
      request = Net::HTTP::Put.new(options[:path])
      request.content_type = options[:type] if options[:type]
      request['Content-Encoding'] = options[:encoding] if options[:encoding]
      request['Accept-Encoding'] = options[:accept_encoding] # even if not set, write a nil to disable decode_content
      assert !request.decode_content, "disabling decode_content failed!"
      
      http = Net::HTTP.new(verm_spawner.hostname, verm_spawner.port)
      http.read_timeout = timeout
      response = http.start do |connection|
        connection.request(request, file_data)
      end
      response.error! unless response.is_a?(Net::HTTPSuccess)
      response
    end

    def put_file(options, verm_spawner = VERM_SPAWNER)
      file_data = File.read(fixture_file_path(options[:file]), :mode => 'rb')
      response = put(options.merge(:data => file_data), verm_spawner)
      location = response['location']
      raise "The location returned was #{location}, but it was supposed to be the requested location #{options[:path]}" if location != options[:path]
      dest_filename = expected_filename(location, options)
      raise "Verm supposedly saved the file to #{dest_filename}, but that doesn't exist" unless File.exist?(dest_filename)
      saved_data = File.read(dest_filename, :mode => 'rb')
      raise "The data saved to file doesn't match the original! #{saved_data.inspect} vs. #{file_data.inspect}" unless saved_data == file_data
      dest_filename
    end


    def get_statistics(options = {})
      while true
        response = get(options.merge(:path => "/_statistics", :expected_response_code => 200))
        lines = response.body.split(/\n/)
        results = lines.inject({}) {|results, line| name, value = line.split(/ /); results[name.to_sym] = value.to_i; results}

        # all our ruby test code is single-threaded, so if there's more than one connection active in the
        # verm instance at a time, it means the previous request our test code made has been responded to
        # and our test code has moved on but the server hasn't yet finished logging and cleaning up the
        # connection.  we need to wait until we have the finished statistics or else we'll get intermittent
        # test failures.
        return results unless results[:connections_current] > 1
        sleep 0.1
      end
    end

    def calculate_statistics_change(before, after)
      results = after.inject({}) {|results, (k, v)| results[k] = v - before[k] unless v == before[k]; results}
    end

    def assert_statistics_change(expected_change, options = {})
      before = get_statistics(options)
      yield
      after = get_statistics(options)

      change = calculate_statistics_change(before, after)
      assert_equal expected_change, change
    end

    def assert_statistics_changes(spawners, expected_changes)
      before = spawners.collect {|spawner| get_statistics(:verm => spawner)}
      yield
      changes = spawners.collect.with_index {|spawner, index| calculate_statistics_change(before[index], get_statistics(:verm => spawner))}
      assert_equal expected_changes, changes
    end
  end
end
